#pragma once
#include "../includes.hpp"

class SaveMacroLayer : public geode::Popup {

    TextInput* authorInput = nullptr;
    TextInput* descInput = nullptr;
    TextInput* nameInput = nullptr;

    CCMenuItemSpriteExtra* formatButton = nullptr;
    CCMenu* formatMenu = nullptr;
    CCLabelBMFont* formatLabel = nullptr;
    int selectedFormat = 0;

private:

    bool setup() {
        Utils::setBackgroundColor(m_bgSprite);

        setTitle("Save Macro");

        cocos2d::CCPoint offset = (CCDirector::sharedDirector()->getWinSize() - m_mainLayer->getContentSize()) / 2;
        m_mainLayer->setPosition(m_mainLayer->getPosition() - offset);
        m_closeBtn->setPosition(m_closeBtn->getPosition() + offset);
        m_bgSprite->setPosition(m_bgSprite->getPosition() + offset);
        m_title->setPosition(m_title->getPosition() + offset);

        CCMenu* menu = CCMenu::create();
        m_mainLayer->addChild(menu);

        authorInput = TextInput::create(104, "Author", "chatFont.fnt");
        authorInput->setPosition({ 61, 42 });
        authorInput->setString(GJAccountManager::sharedState()->m_username.c_str());
        menu->addChild(authorInput);

        CCLabelBMFont* lbl = CCLabelBMFont::create("(optional)", "chatFont.fnt");
        lbl->setPosition({ 61, 20 });
        lbl->setOpacity(73);
        lbl->setScale(0.575);
        menu->addChild(lbl);

        nameInput = TextInput::create(104, "Name", "chatFont.fnt");
        nameInput->setPosition({ -61, 42 });

        nameInput->setString(Global::get().macro.levelInfo.name);

        menu->addChild(nameInput);

        descInput = TextInput::create(226, "Description (optional)", "chatFont.fnt");
        descInput->setPositionY(-8);
        menu->addChild(descInput);

        ButtonSprite* spr = ButtonSprite::create("Save");
        spr->setScale(0.725f);
        CCMenuItemSpriteExtra* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SaveMacroLayer::onSave));
        btn->setPositionY(-56);
        menu->addChild(btn);

        ButtonSprite* formatSprite = ButtonSprite::create("GDR", "goldFont.fnt", "GJ_button_04.png");
        formatSprite->setScale(0.62f);
        formatButton = CCMenuItemSpriteExtra::create(formatSprite, this, menu_selector(SaveMacroLayer::toggleFormatMenu));
        formatButton->setPosition({ 0, -78 });
        menu->addChild(formatButton);

        formatLabel = CCLabelBMFont::create("Format", "chatFont.fnt");
        formatLabel->setScale(0.45f);
        formatLabel->setOpacity(130);
        formatLabel->setPosition({ 0, -55 });
        menu->addChild(formatLabel);

        formatMenu = CCMenu::create();
        formatMenu->setPosition({ 0, -78 });
        formatMenu->setVisible(false);
        menu->addChild(formatMenu);

        const char* formats[] = { "GDR", "JSON", "GDR2", "CMS" };
        for (int i = 0; i < 4; i++) {
            ButtonSprite* optionSprite = ButtonSprite::create(formats[i], "goldFont.fnt", "GJ_button_04.png");
            optionSprite->setScale(0.48f);
            CCMenuItemSpriteExtra* option = CCMenuItemSpriteExtra::create(optionSprite, this, menu_selector(SaveMacroLayer::selectFormat));
            option->setTag(i);
            option->setPosition({ (i - 1.5f) * 58.f, 0 });
            formatMenu->addChild(option);
        }

        return true;
    }

public:

    void toggleFormatMenu(CCObject*) {
        formatMenu->setVisible(!formatMenu->isVisible());
    }

    void selectFormat(CCObject* object) {
        selectedFormat = static_cast<CCMenuItemSpriteExtra*>(object)->getTag();
        const char* formats[] = { "GDR", "JSON", "GDR2", "CMS" };
        formatLabel->setString(formats[selectedFormat]);
        formatMenu->setVisible(false);
        auto* sprite = static_cast<ButtonSprite*>(formatButton->getNormalImage());
        sprite->setString(formats[selectedFormat]);
    }

    STATIC_CREATE(SaveMacroLayer, 285, 194)
    
    static void open() {
        if (Global::get().macro.inputs.empty())
            return FLAlertLayer::create("Save Macro", "You can't save an <cl>empty</c> macro.", "Ok")->show();

        std::filesystem::path path = Mod::get()->getSettingValue<std::filesystem::path>("macros_folder");

        if (!std::filesystem::exists(path)) {
            if (!utils::file::createDirectoryAll(path).isOk())
                return FLAlertLayer::create("Error", ("There was an error getting the folder \"" + path.string() + "\". ID: 10").c_str(), "Ok")->show();
        }

        SaveMacroLayer* layerReal = create();
        layerReal->m_noElasticity = true;
        layerReal->show();
    }

    void onSave(CCObject*) {
        std::string macroName = nameInput->getString();
        if (macroName == "")
            return FLAlertLayer::create("Save Macro", "Give a <cl>name</c> to the macro.", "Ok")->show();

        std::filesystem::path path = Mod::get()->getSettingValue<std::filesystem::path>("macros_folder") / macroName;
        std::string author = authorInput->getString();
        std::string desc = descInput->getString();

        int result = Macro::save(author, desc, path.string(), selectedFormat == 1, selectedFormat == 2, selectedFormat == 3);

        if (result != 0)
            return FLAlertLayer::create("Error", "There was an error saving the macro. ID: " + std::to_string(result), "Ok")->show();

        this->keyBackClicked();
        Notification::create("Macro Saved", NotificationIcon::Success)->show();
    }

};