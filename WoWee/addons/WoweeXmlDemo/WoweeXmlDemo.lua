-- Referenced by the XML through <Script file="..."/>, which is how an addon
-- separates its handlers from its layout. This has to run before the frames are
-- built, because their scripts name these functions.

function WoweeXmlDemo_OnLoad(self)
    self.clicks = 0

    -- The bordered panel look the original interface is built from. The edge
    -- file is a strip of eight square tiles, which is why edgeSize matters as
    -- much as the file itself.
    self:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    self:SetBackdropColor(0.08, 0.08, 0.12, 0.9)
    self:SetBackdropBorderColor(0.9, 0.8, 0.5, 1)

    WoweeXmlDemoBar:SetMinMaxValues(0, 10)
    WoweeXmlDemoBar:SetValue(3)
    WoweeXmlDemoBar:SetStatusBarTexture("Interface\\TargetingFrame\\UI-StatusBar")
    WoweeXmlDemoBar:SetStatusBarColor(0.2, 0.7, 0.2, 1)
    if DEFAULT_CHAT_FRAME then
        DEFAULT_CHAT_FRAME:AddMessage("WoweeXmlDemo: OnLoad ran on " .. tostring(self:GetName()))
    end
end

function WoweeXmlDemo_OnClick(self, button)
    self.clicks = (self.clicks or 0) + 1
    WoweeXmlDemoFrameCount:SetText("clicks: " .. self.clicks)
    -- Each click fills the bar another tenth, so the value plainly drives it.
    WoweeXmlDemoBar:SetValue(self.clicks % 11)
end
