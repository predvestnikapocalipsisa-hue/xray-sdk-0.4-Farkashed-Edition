//----------------------------------------------------
// file: Log.h
//----------------------------------------------------

#ifndef ELogH
#define ELogH
enum TMsgDlgType
{
	mtCustom = 0,
	mtError = 1,
	mtInformation = 2,
	mtConfirmation = 4,

};
enum TMsgDlgButtons
{
	mbYes = 1,
	mbNo = 2,
	mbCancel = 4,
	mbOK = 8,
	mbYesToAll = 16,
	mbNoToAll = 32,
	mrNone = 0,
	mrYes,
	mrNo,
	mrCancel,
	mrOK,
	mrYesToAll,
	mrNoToAll,
};
class ECORE_API CLog
{
public:
	bool in_use;

public:
	CLog() { in_use = false; }
	void Msg(TMsgDlgType mt, LPCSTR _Format, ...);
	int DlgMsg(TMsgDlgType mt, LPCSTR _Format, ...);
	int DlgMsg(TMsgDlgType mt, int btn, LPCSTR _Format, ...);
	void Close();
};

void ECORE_API ELogCallback(LPCSTR txt);

extern ECORE_API CLog ELog;

// Optional override captions for the buttons of the *next*
// DlgMsg(...) call. Set the relevant slot(s) right before calling DlgMsg,
// Slots 0-2 relabel Yes/No/Cancel; slots 3-4 label Yes to All/No to All in a four-choice dialog.
// Leave a slot as NULL to keep the default text. The array is auto-reset
// by MessageDlg() right after the dialog closes.
extern ECORE_API LPCSTR g_DlgMsgBtnCaptions[5];

#endif /*_INCDEF_NETDEVICELOG_H_*/
