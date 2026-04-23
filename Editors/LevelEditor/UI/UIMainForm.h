#pragma once
typedef void (*SplashAnimCallback)(ImDrawList* drawList,
                                   ImVec2      origin,
                                   ImVec2      size,
                                   float       time);

class UIMainForm : public XrUI
{
public:
	UIMainForm();
	virtual ~UIMainForm();
	virtual void Draw();
	bool Frame();
	IC UILeftBarForm     *GetLeftBarForm()    { return m_LeftBar;     }
	IC UITopBarForm      *GetTopBarForm()     { return m_TopBar;      }
	IC UIRenderForm      *GetRenderForm()     { return m_Render;      }
	IC UILPropertiesFrom *GetPropertiesFrom() { return m_Properties;  }

	static bool              s_showSplash;
	static void*             g_splash_tex;
	static float             s_splashAlpha;   
	static SplashAnimCallback g_animCallback;  

	static void SetSplashAnimCallback(SplashAnimCallback fn) { g_animCallback = fn; }

private:
	UITopBarForm      *m_TopBar;
	UIRenderForm      *m_Render;
	UIMainMenuForm    *m_MainMenu;
	UILeftBarForm     *m_LeftBar;
	UILPropertiesFrom *m_Properties;

private:
	void DrawContextMenu();
	void DrawSplash();
};
extern UIMainForm *MainForm;
