//----------------------------------------------------
// file: NetDeviceELog.cpp
//----------------------------------------------------

#include "stdafx.h"
#pragma hdrstop

#include "ELog.h"
#ifdef _EDITOR
#include "UILogForm.h"
#include "ui_main.h"
void ELogCallback(LPCSTR txt)
{
	if (0 == txt[0])
		return;
	bool bDlg = ('#' == txt[0]) || ((0 != txt[1]) && ('#' == txt[1]));
	TMsgDlgType mt = ('!' == txt[0]) || ((0 != txt[1]) && ('!' == txt[1])) ? mtError : mtInformation;
	if (('!' == txt[0]) || ('#' == txt[0]))
		txt++;
	if (('!' == txt[0]) || ('#' == txt[0]))
		txt++;
	if (bDlg)
		UILogForm::AddDlgMessage(mt, txt);
	else
		UILogForm::AddMessage(mt, txt);
}
#endif
#ifdef _LW_EXPORT
#include <lwhost.h>
extern "C" LWMessageFuncs* g_msg;
void ELogCallback(LPCSTR txt)
{
	if (0 == txt[0])
		return;
	bool bDlg = ('#' == txt[0]) || ((0 != txt[1]) && ('#' == txt[1]));
	if (bDlg)
	{
		int mt = ('!' == txt[0]) || ((0 != txt[1]) && ('!' == txt[1])) ? 1 : 0;
		if (('!' == txt[0]) || ('#' == txt[0]))
			txt++;
		if (('!' == txt[0]) || ('#' == txt[0]))
			txt++;
		if (mt == 1)
			g_msg->error(txt, 0);
		else
			g_msg->info(txt, 0);
	}
}
#endif
#ifdef _MAX_EXPORT
#include "NetDeviceLog.h"
void ELogCallback(LPCSTR txt)
{
	if (0 != txt[0])
	{
		if (txt[0] == '!')
			EConsole.print(mtError, txt + 1);
		else
			EConsole.print(mtInformation, txt);
	}
}
#endif
#ifdef _MAYA_PLUGIN
void ELogCallback(LPCSTR txt)
{
	if (0 != txt[0])
	{
		if (txt[0] == '!')
			std::cerr << "XR-Error: " << txt + 1 << "\n";
		else
			std::cerr << "XR-Info: " << txt << "\n";
	}
	//.		MStringArray res;
	//		MGlobal::executeCommand("confirmDialog -title \"Error\" -message \"Mesh have non-triangulated polygon.\" -button \"Ok\" -defaultButton \"Ok\"",res);
}
#endif

//----------------------------------------------------
CLog ELog;
LPCSTR g_DlgMsgBtnCaptions[3] = { 0, 0, 0 }; // Yes, No, Cancel — see ELog.h
//----------------------------------------------------
static HHOOK s_DlgMsgHook = NULL;

// Relabels standard MessageBox buttons (Yes/No/Cancel) right after the
// dialog is created, using whatever is set in g_DlgMsgBtnCaptions.
// This is the only way to change a stock MessageBox button's text without
// switching the whole codebase over to TaskDialogIndirect.
static LRESULT CALLBACK DlgMsgCBTProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HCBT_ACTIVATE)
	{
		HWND hwnd = (HWND)wParam;
		char cls[64];
		GetClassNameA(hwnd, cls, sizeof(cls));
		if (0 == _stricmp(cls, "#32770")) // standard MessageBox dialog class
		{
			struct
			{
				int ctrl_id;
				int caption_idx;
			} map[3] = { { IDYES, 0 }, { IDNO, 1 }, { IDCANCEL, 2 } };

			for (int i = 0; i < 3; ++i)
			{
				if (g_DlgMsgBtnCaptions[map[i].caption_idx])
				{
					HWND btn = GetDlgItem(hwnd, map[i].ctrl_id);
					if (btn)
						SetWindowTextA(btn, g_DlgMsgBtnCaptions[map[i].caption_idx]);
				}
			}
		}
	}
	return CallNextHookEx(s_DlgMsgHook, nCode, wParam, lParam);
}
//----------------------------------------------------
inline TMsgDlgButtons MessageDlg(const char* text, TMsgDlgType mt, int btn)
{
	UINT Flags = 0;
	const char* Title = "";
	switch (mt)
	{
	case mtCustom:
		break;
	case mtError:
		Title = "Error";
		Flags = MB_ICONERROR;
		break;
	case mtInformation:
		Title = "Info";
		Flags = MB_ICONINFORMATION;
		break;
	case mtConfirmation:
		Title = "Warning";
		Flags = MB_ICONWARNING;
		break;
	default:
		R_ASSERT(0);
		break;
	}
	if (btn == mbYes)
	{
		Flags |= MB_OK;
	}
	if (btn == mbOK)
	{
		Flags |= MB_OK;
	}
	else if (btn == (mbYes | mbNo))
	{
		Flags |= MB_YESNO;
	}
	else if (btn == (mbYes | mbNo | mbCancel))
	{
		Flags |= MB_YESNOCANCEL;
	}
	else
	{
		R_ASSERT(0);
	}
	bool need_caption_hook = g_DlgMsgBtnCaptions[0] || g_DlgMsgBtnCaptions[1] || g_DlgMsgBtnCaptions[2];
	if (need_caption_hook)
		s_DlgMsgHook = SetWindowsHookEx(WH_CBT, DlgMsgCBTProc, NULL, GetCurrentThreadId());

	int msgboxID = MessageBox(
		NULL,
		text,
		Title,
		Flags);

	if (need_caption_hook)
	{
		UnhookWindowsHookEx(s_DlgMsgHook);
		s_DlgMsgHook = NULL;
		g_DlgMsgBtnCaptions[0] = g_DlgMsgBtnCaptions[1] = g_DlgMsgBtnCaptions[2] = 0;
	}
	switch (msgboxID)
	{
	case IDCANCEL:
		return mrCancel; // TODO: add code
		break;
	case IDYES:
		return mrYes;
		break;
	case IDNO:
		return mrNo;
		break;
	case IDOK:
		return mrOK;
		break;
	}
	if (btn | mbCancel)
		return mrCancel;
	if (btn | mbNo)
		return mrNo;
	if (btn == mbOK)
		return mrOK;
	return mrYes;
}
int CLog::DlgMsg(TMsgDlgType mt, int btn, LPCSTR _Format, ...)
{
	in_use = true;
	char buf[4096];
	va_list l;
	va_start(l, _Format);
	vsprintf(buf, _Format, l);

	int res = 0;
#ifdef _EDITOR
	ExecCommand(COMMAND_RENDER_FOCUS);

	res = MessageDlg(buf, mt, btn);
	if (mtConfirmation == mt)
	{
		switch (res)
		{
		case mrYes:
			strcat(buf, " - Yes.");
			break;
		case mrNo:
			strcat(buf, " - No.");
			break;
		case mrCancel:
			strcat(buf, " - Cancel.");
			break;
		default:
			strcat(buf, " - Something.");
		}
	}
#endif
#ifdef _LW_EXPORT
	switch (mt)
	{
	case mtError:
		g_msg->error(buf, 0);
		break;
	case mtInformation:
		g_msg->info(buf, 0);
		break;
	default:
		g_msg->info(buf, 0);
		break;
	}
#endif
#ifdef _MAX_PLUGIN
	switch (mt)
	{
	case mtError:
		MessageBox(0, buf, "Error", MB_OK | MB_ICONERROR);
		break;
	case mtInformation:
		MessageBox(0, buf, "Information", MB_OK | MB_ICONINFORMATION);
		break;
	default:
		MessageBox(0, buf, "Information", MB_OK | MB_ICONINFORMATION);
		break;
	}
#endif

	Msg(mt, buf);

	in_use = false;

	return res;
}

void CLog::Close()
{
	SetLogCB(0);
	UILogForm::Destroy();
}

int CLog::DlgMsg(TMsgDlgType mt, LPCSTR _Format, ...)
{
	in_use = true;
	char buf[4096];
	va_list l;
	va_start(l, _Format);
	vsprintf(buf, _Format, l);

	int res = 0;
#ifdef _EDITOR
	ExecCommand(COMMAND_RENDER_FOCUS);

	if (mtConfirmation == mt)
		res = MessageDlg(buf, mt, mbYes | mbNo | mbCancel);
	else
		res = MessageDlg(buf, mt, mbOK);

	if (mtConfirmation == mt)
	{
		switch (res)
		{
		case mrYes:
			strcat(buf, " - Yes.");
			break;
		case mrNo:
			strcat(buf, " - No.");
			break;
		case mrCancel:
			strcat(buf, " - Cancel.");
			break;
		default:
			strcat(buf, " - Something.");
		}
	}
#endif
#ifdef _LW_EXPORT
	switch (mt)
	{
	case mtError:
		g_msg->error(buf, 0);
		break;
	case mtInformation:
		g_msg->info(buf, 0);
		break;
	default:
		g_msg->info(buf, 0);
		break;
	}
#endif
#ifdef _MAX_PLUGIN
	switch (mt)
	{
	case mtError:
		MessageBox(0, buf, "Error", MB_OK | MB_ICONERROR);
		break;
	case mtInformation:
		MessageBox(0, buf, "Information", MB_OK | MB_ICONINFORMATION);
		break;
	default:
		MessageBox(0, buf, "Information", MB_OK | MB_ICONINFORMATION);
		break;
	}
#endif

	Msg(mt, buf);

	in_use = false;

	return res;
}

void CLog::Msg(TMsgDlgType mt, LPCSTR _Format, ...)
{
	char buf[4096];
	va_list l;
	va_start(l, _Format);
	vsprintf(buf, _Format, l);

#ifdef _EDITOR
	UILogForm::AddMessage(mt, xr_string(buf));
#endif
#ifdef _MAX_EXPORT
	EConsole.print(mt, buf);
#endif
#ifdef _LW_EXPORT
	switch (mt)
	{
	case mtError:
		g_msg->error(buf, 0);
		break;
	}
#endif

	::LogExecCB = FALSE;
	::Msg(buf);
	::LogExecCB = TRUE;
}
//----------------------------------------------------