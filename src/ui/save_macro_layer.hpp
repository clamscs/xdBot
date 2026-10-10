#pragma once
#include "../includes.hpp"

class SaveMacroLayer : public geode::Popup {

    TextInput* authorInput = nullptr;
    TextInput* descInput = nullptr;
    TextInput* nameInput = nullptr;

    CCMenuItemToggler* gdrToggle = nullptr;
    CCMenuItemToggler* jsonToggle = nullptr;
    CCMenuItemToggler* gdr2Toggle = nullptr;
    CCMenuItemToggler* cmsToggle = nullptr;

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

        CCSprite* spriteOn = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        CCSprite* spriteOff = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
        gdrToggle = CCMenuItemToggler::create(spriteOff, spriteOn, this, menu_selector(SaveMacroLayer::toggleGDR));
        gdrToggle->setPosition({ -105, -78 });
        gdrToggle->setScale(0.575);
        gdrToggle->toggle(true);
        menu->addChild(gdrToggle);

        lbl = CCLabelBMFont::create("GDR", "bigFont.fnt");
        lbl->setPosition({ -82, -77.5 });
        lbl->setScale(0.375);
        menu->addChild(lbl);

        jsonToggle = CCMenuItemToggler::create(spriteOff, spriteOn, this, menu_selector(SaveMacroLayer::toggleJSON));
        jsonToggle->setPosition({ -35, -78 });
        jsonToggle->setScale(0.575);
        menu->addChild(jsonToggle);

        lbl = CCLabelBMFont::create("JSON", "bigFont.fnt");
        lbl->setPosition({ -17, -77.5 });
        lbl->setScale(0.375);
        menu->addChild(lbl);

        gdr2Toggle = CCMenuItemToggler::create(spriteOff, spriteOn, this, menu_selector(SaveMacroLayer::toggleGDR2));
        gdr2Toggle->setPosition({ 35, -78 });
        gdr2Toggle->setScale(0.575);
        menu->addChild(gdr2Toggle);

        lbl = CCLabelBMFont::create("GDR2", "bigFont.fnt");
        lbl->setPosition({ 58, -77.5 });
        lbl->setScale(0.375);
        menu->addChild(lbl);

        cmsToggle = CCMenuItemToggler::create(spriteOff, spriteOn, this, menu_selector(SaveMacroLayer::toggleCMS));
        cmsToggle->setPosition({ 103, -78 });
        cmsToggle->setScale(0.575);
        menu->addChild(cmsToggle);

        lbl = CCLabelBMFont::create("CMS", "bigFont.fnt");
        lbl->setPosition({ 126, -77.5 });
        lbl->setScale(0.375);
        menu->addChild(lbl);

        return true;
    }

public:

    void clearFormats(CCMenuItemToggler* active) {
        if (active != gdrToggle) gdrToggle->toggle(false);
        if (active != jsonToggle) jsonToggle->toggle(false);
        if (active != gdr2Toggle) gdr2Toggle->toggle(false);
        if (active != cmsToggle) cmsToggle->toggle(false);
    }

    void toggleGDR(CCObject*) {
        if (gdrToggle->isToggled()) clearFormats(gdrToggle);
    }

    void toggleJSON(CCObject*) {
        if (jsonToggle->isToggled()) clearFormats(jsonToggle);
    }

    void toggleGDR2(CCObject*) {
        if (gdr2Toggle->isToggled()) clearFormats(gdr2Toggle);
    }

    void toggleCMS(CCObject*) {
        if (cmsToggle->isToggled()) clearFormats(cmsToggle);
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

        int result = Macro::save(author, desc, path.string(), jsonToggle->isToggled(), gdr2Toggle->isToggled(), cmsToggle->isToggled());

        if (result != 0)
            return FLAlertLayer::create("Error", "There was an error saving the macro. ID: " + std::to_string(result), "Ok")->show();

        this->keyBackClicked();
        Notification::create("Macro Saved", NotificationIcon::Success)->show();
    }

};