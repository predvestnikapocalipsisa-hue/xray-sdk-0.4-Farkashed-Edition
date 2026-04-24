#pragma once

class UIContentBrowser; // Форвард-декларация

class UIMainMenuForm : public XrUI
{
private:
    UIContentBrowser* m_ContentBrowser; // Добавляем переменную

public:
    UIMainMenuForm();
    virtual ~UIMainMenuForm();
    virtual void Draw();

    UIContentBrowser* GetContentBrowser() { return m_ContentBrowser; }
};