#pragma once

#include "game/world_packets.hpp"
#include "game/opcode_table.hpp"
#include "game/inventory.hpp"
#include "game/handler_types.hpp"
#include "network/packet.hpp"
#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wowee {
namespace game {

class GameHandler;

class InventoryHandler {
public:
    using PacketHandler = std::function<void(network::Packet&)>;
    using DispatchTable = std::unordered_map<LogicalOpcode, PacketHandler>;

    explicit InventoryHandler(GameHandler& owner);

    void registerOpcodes(DispatchTable& table);

    // ---- Item text (books / readable items) ----
    bool isItemTextOpen() const { return itemTextOpen_; }
    const std::string& getItemText() const { return itemText_; }
    void closeItemText() { itemTextOpen_ = false; }
    void queryItemText(uint64_t itemGuid);

    // ---- Trade ----
    enum class TradeStatus : uint8_t {
        None = 0, PendingIncoming, Open, Accepted, Complete
    };

    // WoW trade window: 7 slots total. Slots 0-5 are transferred to the partner;
    // slot 6 (TRADE_SLOT_NONTRADED) is the "will not be traded" slot — you place your
    // own item there so the partner can enchant/craft on it without it changing hands.
    static constexpr int TRADE_SLOT_COUNT        = 7;
    static constexpr int TRADE_SLOT_TRADED_COUNT = 6;
    static constexpr int TRADE_SLOT_NONTRADED    = 6;

    struct TradeSlot {
        uint32_t itemId      = 0;
        uint32_t displayId   = 0;
        uint32_t stackCount  = 0;
        uint64_t itemGuid    = 0;
    };

    TradeStatus getTradeStatus() const { return tradeStatus_; }
    bool hasPendingTradeRequest() const { return tradeStatus_ == TradeStatus::PendingIncoming; }
    bool isTradeOpen() const { return tradeStatus_ == TradeStatus::Open || tradeStatus_ == TradeStatus::Accepted; }
    const std::string& getTradePeerName() const { return tradePeerName_; }
    const std::array<TradeSlot, TRADE_SLOT_COUNT>& getMyTradeSlots() const { return myTradeSlots_; }
    const std::array<TradeSlot, TRADE_SLOT_COUNT>& getPeerTradeSlots() const { return peerTradeSlots_; }
    uint64_t getMyTradeGold() const { return myTradeGold_; }
    uint64_t getPeerTradeGold() const { return peerTradeGold_; }
    void acceptTradeRequest();
    void declineTradeRequest();
    void acceptTrade();
    void cancelTrade();
    void setTradeItem(uint8_t tradeSlot, uint8_t srcBag, uint8_t srcSlot);
    void clearTradeItem(uint8_t tradeSlot);
    void setTradeGold(uint64_t amount);

    // ---- Loot ----
    void lootTarget(uint64_t targetGuid);
    void lootItem(uint8_t slotIndex);
    void closeLoot();
    bool isLootWindowOpen() const { return lootWindowOpen_; }
    const LootResponseData& getCurrentLoot() const { return currentLoot_; }
    void setAutoLoot(bool enabled) { autoLoot_ = enabled; }
    bool isAutoLoot() const { return autoLoot_; }
    void setAutoSellGrey(bool enabled) { autoSellGrey_ = enabled; }
    bool isAutoSellGrey() const { return autoSellGrey_; }
    void setAutoRepair(bool enabled) { autoRepair_ = enabled; }
    bool isAutoRepair() const { return autoRepair_; }

    // Master loot candidates (from SMSG_LOOT_MASTER_LIST)
    const std::vector<uint64_t>& getMasterLootCandidates() const { return masterLootCandidates_; }
    bool hasMasterLootCandidates() const { return !masterLootCandidates_.empty(); }
    void lootMasterGive(uint8_t lootSlot, uint64_t targetGuid);

    // Group loot roll (aliased from handler_types.hpp)
    using LootRollEntry = game::LootRollEntry;
    bool hasPendingLootRoll() const { return pendingLootRollActive_; }
    const LootRollEntry& getPendingLootRoll() const { return pendingLootRoll_; }
    void sendLootRoll(uint64_t objectGuid, uint32_t slot, uint8_t rollType);

    // ---- Equipment Sets (aliased from handler_types.hpp) ----
    using EquipmentSetInfo = game::EquipmentSetInfo;
    const std::vector<EquipmentSetInfo>& getEquipmentSets() const { return equipmentSetInfo_; }
    bool supportsEquipmentSets() const;
    void useEquipmentSet(uint32_t setId);
    void saveEquipmentSet(const std::string& name, const std::string& iconName = "INV_Misc_QuestionMark",
                          uint64_t existingGuid = 0, uint32_t setIndex = 0xFFFFFFFF);
    void deleteEquipmentSet(uint64_t setGuid);

    // ---- Vendor ----
    struct BuybackItem {
        uint64_t itemGuid = 0;
        ItemDef item;
        uint32_t count = 1;
        // Inventory slot sent in CMSG_BUYBACK_ITEM (74..85).  This is server
        // identity, not the item's current row in the buyback UI.
        uint32_t wireSlot = 0;
    };
    void openVendor(uint64_t npcGuid);
    void closeVendor();
    // Drop the session-persistent buyback mirror (fresh character world entry).
    void clearBuybackState();
    void buyItem(uint64_t vendorGuid, uint32_t itemId, uint32_t slot, uint32_t count);
    void sellItem(uint64_t vendorGuid, uint64_t itemGuid, uint32_t count);
    void sellItemBySlot(int backpackIndex);
    void sellItemInBag(int bagIndex, int slotIndex);
    void buyBackItem(uint32_t buybackSlot);
    void repairItem(uint64_t vendorGuid, uint64_t itemGuid);
    void repairAll(uint64_t vendorGuid, bool useGuildBank = false);
    uint32_t estimateRepairAllCost() const;
    const std::deque<BuybackItem>& getBuybackItems() const { return buybackItems_; }
    void autoEquipItemBySlot(int backpackIndex);
    void autoEquipItemInBag(int bagIndex, int slotIndex);
    void useItemBySlot(int backpackIndex);
    void useItemInBag(int bagIndex, int slotIndex);

    // ---- Item-targeted item use (sharpening stones, weightstones, weapon oils) ----
    /// True while a used item is waiting for the player to pick the item it applies to.
    bool isAwaitingItemTarget() const;
    /// Entry of the item awaiting a target (0 if none) — drives the targeting cursor.
    uint32_t getPendingItemTargetSourceItemId() const;
    void cancelItemTargeting();

    /// Arm item targeting for a spell that must be cast at an item. The cast is
    /// sent once the player picks one.
    void beginSpellItemTargeting(uint32_t spellId, const std::string& spellName);
    /// Sends the parked CMSG_USE_ITEM with TARGET_FLAG_ITEM against targetItemGuid.
    void completeItemUseOnItem(uint64_t targetItemGuid);

    void openItemBySlot(int backpackIndex);
    void openItemInBag(int bagIndex, int slotIndex);
    void readItemBySlot(int backpackIndex);
    void readItemInBag(int bagIndex, int slotIndex);
    void destroyItem(uint8_t bag, uint8_t slot, uint8_t count = 1);
    void splitItem(uint8_t srcBag, uint8_t srcSlot, uint8_t count);
    void swapContainerItems(uint8_t srcBag, uint8_t srcSlot, uint8_t dstBag, uint8_t dstSlot);
    void swapBagSlots(int srcBagIndex, int dstBagIndex);
    void unequipToBackpack(EquipSlot equipSlot);
    void useItemById(uint32_t itemId);
    bool isVendorWindowOpen() const { return vendorWindowOpen_; }
    const ListInventoryData& getVendorItems() const { return currentVendorItems_; }
    void setVendorCanRepair(bool v) { currentVendorItems_.canRepair = v; }
    uint64_t getVendorGuid() const { return currentVendorItems_.vendorGuid; }

    // ---- Mail ----
    // Slots the UI can hold. What can actually be sent depends on the wire
    // format — see maxSendableMailAttachments().
    static constexpr int MAIL_MAX_ATTACHMENTS = 12;

    /// How many attachments this expansion's CMSG_SEND_MAIL can carry. Vanilla
    /// writes a single item GUID, so anything past the first was silently left
    /// in the player's bags while the UI happily accepted twelve.
    static int maxSendableMailAttachments();
    struct MailAttachSlot {
        uint64_t itemGuid = 0;
        game::ItemDef item;
        uint8_t srcBag = 0xFF;
        uint8_t srcSlot = 0;
        bool occupied() const { return itemGuid != 0; }
    };
    bool isMailboxOpen() const { return mailboxOpen_; }
    const std::vector<MailMessage>& getMailInbox() const { return mailInbox_; }
    int getSelectedMailIndex() const { return selectedMailIndex_; }
    void setSelectedMailIndex(int idx) { selectedMailIndex_ = idx; }
    bool isMailComposeOpen() const { return showMailCompose_; }
    void openMailCompose() { showMailCompose_ = true; clearMailAttachments(); }
    void closeMailCompose() { showMailCompose_ = false; clearMailAttachments(); }
    bool hasNewMail() const { return hasNewMail_; }
    void openMailbox(uint64_t guid);
    void closeMailbox();
    void sendMail(const std::string& recipient, const std::string& subject,
                  const std::string& body, uint64_t money, uint64_t cod = 0);
    bool attachItemFromBackpack(int backpackIndex);
    bool attachItemFromBag(int bagIndex, int slotIndex);
    bool detachMailAttachment(int attachIndex);
    void clearMailAttachments();
    const std::array<MailAttachSlot, 12>& getMailAttachments() const { return mailAttachments_; }
    int getMailAttachmentCount() const;
    void mailTakeMoney(uint32_t mailId);
    void mailTakeItem(uint32_t mailId, uint32_t itemGuidLow);
    void mailDelete(uint32_t mailId);
    void mailMarkAsRead(uint32_t mailId);
    void refreshMailList();

    // ---- Bank ----
    void openBank(uint64_t guid);
    void closeBank();
    void buyBankSlot();
    // Cost in copper to purchase the bank bag slot at the given 0-based index
    // (BankBagSlotPrices.dbc; identical values across Classic/TBC/WotLK). 0 = free/invalid.
    static uint32_t getBankBagSlotPrice(int slotIndex);
    void depositItem(uint8_t srcBag, uint8_t srcSlot);
    void withdrawItem(uint8_t srcBag, uint8_t srcSlot);
    bool isBankOpen() const { return bankOpen_; }
    uint64_t getBankerGuid() const { return bankerGuid_; }
    int getEffectiveBankSlots() const { return effectiveBankSlots_; }
    int getEffectiveBankBagSlots() const { return effectiveBankBagSlots_; }

    // ---- Guild Bank ----
    void openGuildBank(uint64_t guid);
    void closeGuildBank();
    void queryGuildBankTab(uint8_t tabId);
    void buyGuildBankTab();
    void depositGuildBankMoney(uint32_t amount);
    void withdrawGuildBankMoney(uint32_t amount);
    void guildBankWithdrawItem(uint8_t tabId, uint8_t bankSlot, uint8_t destBag, uint8_t destSlot);
    void guildBankDepositItem(uint8_t tabId, uint8_t bankSlot, uint8_t srcBag, uint8_t srcSlot);
    // Deposit an inventory item into the first free slot of the viewed tab.
    void guildBankDepositFromInventory(uint8_t srcBag, uint8_t srcSlot);
    bool isGuildBankOpen() const { return guildBankOpen_; }
    const GuildBankData& getGuildBankData() const { return guildBankData_; }
    uint8_t getGuildBankActiveTab() const { return guildBankActiveTab_; }
    void setGuildBankActiveTab(uint8_t tab) { guildBankActiveTab_ = tab; }

    // ---- Auction House ----
    void openAuctionHouse(uint64_t guid);
    void closeAuctionHouse();
    void auctionSearch(const std::string& name, uint8_t levelMin, uint8_t levelMax,
                       uint32_t quality, uint32_t itemClass, uint32_t itemSubClass,
                       uint32_t invTypeMask, uint8_t usableOnly, uint32_t offset = 0);
    void auctionSellItem(int backpackIndex, uint32_t bid,
                         uint32_t buyout, uint32_t duration);
    // Post an auction for an item identified by its server GUID, so items in any
    // container (backpack or equipped bags) can be listed, not just the backpack.
    void auctionSellItemByGuid(uint64_t itemGuid, uint32_t stackCount, uint32_t bid,
                               uint32_t buyout, uint32_t duration);
    void auctionPlaceBid(uint32_t auctionId, uint32_t amount);
    void auctionBuyout(uint32_t auctionId, uint32_t buyoutPrice);
    void auctionCancelItem(uint32_t auctionId);
    void auctionListOwnerItems(uint32_t offset = 0);
    void auctionListBidderItems(uint32_t offset = 0);
    bool isAuctionHouseOpen() const { return auctionOpen_; }
    uint64_t getAuctioneerGuid() const { return auctioneerGuid_; }
    const AuctionListResult& getAuctionBrowseResults() const { return auctionBrowseResults_; }
    const AuctionListResult& getAuctionOwnerResults() const { return auctionOwnerResults_; }
    const AuctionListResult& getAuctionBidderResults() const { return auctionBidderResults_; }
    int getAuctionActiveTab() const { return auctionActiveTab_; }
    void setAuctionActiveTab(int tab) { auctionActiveTab_ = tab; }
    float getAuctionSearchDelay() const { return auctionSearchDelayTimer_; }
    // Ticked from GameHandler::update — this member is the authoritative
    // timer (GameHandler used to decrement its own never-set copy, leaving
    // the search button disabled forever after the first search).
    void tickAuctionSearchDelay(float deltaTime) {
        if (auctionSearchDelayTimer_ > 0.0f) {
            auctionSearchDelayTimer_ -= deltaTime;
            if (auctionSearchDelayTimer_ < 0.0f) auctionSearchDelayTimer_ = 0.0f;
        }
    }

    // ---- Trainer ----
    struct TrainerTab {
        std::string name;
        std::vector<const TrainerSpell*> spells;
    };
    bool isTrainerWindowOpen() const { return trainerWindowOpen_; }
    const TrainerListData& getTrainerSpells() const { return currentTrainerList_; }
    void trainSpell(uint32_t spellId);
    void closeTrainer();
    const std::vector<TrainerTab>& getTrainerTabs() const { return trainerTabs_; }
    void resetTradeState();

    // ---- Methods moved from GameHandler ----
    void initiateTrade(uint64_t targetGuid);
    uint32_t getTempEnchantRemainingMs(uint32_t slot) const;
    void addMoneyCopper(uint32_t amount);

    // ---- Inventory field / rebuild methods (moved from GameHandler) ----
    void queryItemInfo(uint32_t entry, uint64_t guid);
    uint64_t resolveOnlineItemGuid(uint32_t itemId) const;
    void detectInventorySlotBases(const FlatFieldMap& fields);
    bool applyInventoryFields(const FlatFieldMap& fields);
    void extractContainerFields(uint64_t containerGuid, const FlatFieldMap& fields);
    ItemDef buildItemDef(uint32_t entry, uint32_t stackCount, uint32_t curDur, uint32_t maxDur, uint64_t guid,
                         uint32_t flags = 0, int32_t randomPropertyId = 0, uint32_t suffixFactor = 0);
    void rebuildOnlineInventory();
    void maybeDetectVisibleItemLayout();
    void updateOtherPlayerVisibleItems(uint64_t guid, const FlatFieldMap& fields);
    void cacheInspectedPlayerEquipment(uint64_t guid, const std::array<uint32_t, 19>& itemEntries);
    void emitOtherPlayerEquipment(uint64_t guid);
    void emitAllOtherPlayerEquipment();
    void handleItemQueryResponse(network::Packet& packet);

private:
    // --- Packet handlers ---
    void handleLootResponse(network::Packet& packet);
    void handleLootReleaseResponse(network::Packet& packet);
    void handleLootRemoved(network::Packet& packet);
    void handleListInventory(network::Packet& packet);
    void handleTrainerList(network::Packet& packet);
    void handleItemTextQueryResponse(network::Packet& packet);
    void handleTradeStatus(network::Packet& packet);
    void handleTradeStatusExtended(network::Packet& packet);
    void handleLootRoll(network::Packet& packet);
    void handleLootRollWon(network::Packet& packet);
    void handleShowBank(network::Packet& packet);
    void handleBuyBankSlotResult(network::Packet& packet);
    void handleGuildBankList(network::Packet& packet);
    void handleAuctionHello(network::Packet& packet);
    void handleAuctionListResult(network::Packet& packet);
    void handleAuctionOwnerListResult(network::Packet& packet);
    void handleAuctionBidderListResult(network::Packet& packet);
    void handleAuctionCommandResult(network::Packet& packet);
    void handleShowMailbox(network::Packet& packet);
    void handleMailListResult(network::Packet& packet);
    void handleSendMailResult(network::Packet& packet);
    void handleReceivedMail(network::Packet& packet);
    void handleQueryNextMailTime(network::Packet& packet);
    // Update the unread-mail flag, announcing (chat + sound) once on the rising edge.
    void setHasNewMail(bool value);
    void handleEquipmentSetList(network::Packet& packet);

    void categorizeTrainerSpells();
    void handleTrainerBuySucceeded(network::Packet& packet);
    void handleTrainerBuyFailed(network::Packet& packet);

    // Resolves the item's on-use spell, then either parks the use for item
    // targeting or sends it immediately.
    void dispatchUseItem(uint8_t wowBag, uint8_t wowSlot, uint64_t itemGuid, const ItemDef& item);
    void sendUseItem(uint8_t wowBag, uint8_t wowSlot, uint64_t itemGuid, uint32_t spellId,
                     uint64_t targetGuid, uint64_t itemTargetGuid);

    GameHandler& owner_;

    // ---- Item-targeted item use ----
    struct PendingItemTarget {
        uint8_t  bag = 0xFF;
        uint8_t  slot = 0;
        uint64_t itemGuid = 0;
        uint32_t spellId = 0;
        uint32_t itemId = 0;
        std::string itemName;
        /// True when a spell is waiting for its item, rather than an item being
        /// applied to another item. Disenchant, Prospecting, Milling and the
        /// enchant formulas all take an item and are cast, not used.
        bool fromSpell = false;
    };
    // mutable: isAwaitingItemTarget() drops the pending use when out of world.
    mutable std::optional<PendingItemTarget> pendingItemTarget_;

    // Per-equip-slot (permanentEnchant << 32 | temporaryEnchant), so an enchant
    // change marks equipment dirty even though the displayInfoId is unchanged.
    std::array<uint64_t, 19> lastEquipEnchantIds_{};

    // ---- Item text state ----
    bool        itemTextOpen_   = false;
    std::string itemText_;

    // ---- Trade state ----
    TradeStatus tradeStatus_  = TradeStatus::None;
    uint64_t    tradePeerGuid_= 0;
    std::string tradePeerName_;
    std::array<TradeSlot, TRADE_SLOT_COUNT> myTradeSlots_{};
    std::array<TradeSlot, TRADE_SLOT_COUNT> peerTradeSlots_{};
    uint64_t myTradeGold_   = 0;
    uint64_t peerTradeGold_ = 0;

    // ---- Loot state ----
    bool lootWindowOpen_ = false;
    bool autoLoot_ = false;
    bool autoSellGrey_ = false;
    bool autoRepair_ = false;
    LootResponseData currentLoot_;
    std::vector<uint64_t> masterLootCandidates_;

    // Group loot roll state
    bool          pendingLootRollActive_ = false;
    LootRollEntry pendingLootRoll_;
    struct LocalLootState {
        LootResponseData data;
        bool moneyTaken = false;
        bool itemAutoLootSent = false;
    };
    std::unordered_map<uint64_t, LocalLootState> localLootState_;
    uint64_t pendingLootMoneyGuid_ = 0;
    uint32_t pendingLootMoneyAmount_ = 0;
    float pendingLootMoneyNotifyTimer_ = 0.0f;
    std::unordered_map<uint64_t, float> recentLootMoneyAnnounceCooldowns_;

    // ---- Vendor state ----
    bool vendorWindowOpen_ = false;
    ListInventoryData currentVendorItems_;
    std::deque<BuybackItem> buybackItems_;
    std::unordered_map<uint64_t, BuybackItem> pendingSellToBuyback_;
    std::array<uint64_t, 12> buybackSlotGuids_{};
    int pendingBuybackSlot_ = -1;
    uint32_t pendingBuybackWireSlot_ = 0;
    uint32_t pendingBuyItemId_ = 0;
    uint32_t pendingBuyItemSlot_ = 0;

    void reconcileBuybackSlots();

    // ---- Mail state ----
    bool mailboxOpen_ = false;
    uint64_t mailboxGuid_ = 0;
    std::vector<MailMessage> mailInbox_;
    int selectedMailIndex_ = -1;
    bool showMailCompose_ = false;
    bool hasNewMail_ = false;
    std::array<MailAttachSlot, MAIL_MAX_ATTACHMENTS> mailAttachments_{};

    // ---- Bank state ----
    bool bankOpen_ = false;
    uint64_t bankerGuid_ = 0;
    std::array<uint64_t, 28> bankSlotGuids_{};
    std::array<uint64_t, 7> bankBagSlotGuids_{};
    int effectiveBankSlots_ = 28;
    int effectiveBankBagSlots_ = 7;

    // ---- Guild Bank state ----
    bool guildBankOpen_ = false;
    uint64_t guildBankerGuid_ = 0;
    GuildBankData guildBankData_;
    uint8_t guildBankActiveTab_ = 0;

    // ---- Auction House state ----
    bool auctionOpen_ = false;
    uint64_t auctioneerGuid_ = 0;
    uint32_t auctionHouseId_ = 0;
    AuctionListResult auctionBrowseResults_;
    AuctionListResult auctionOwnerResults_;
    AuctionListResult auctionBidderResults_;
    int auctionActiveTab_ = 0;
    float auctionSearchDelayTimer_ = 0.0f;
    struct AuctionSearchParams {
        std::string name;
        uint8_t levelMin = 0, levelMax = 0;
        uint32_t quality = 0xFFFFFFFF;
        uint32_t itemClass = 0xFFFFFFFF;
        uint32_t itemSubClass = 0xFFFFFFFF;
        uint32_t invTypeMask = 0;
        uint8_t usableOnly = 0;
        uint32_t offset = 0;
    };
    AuctionSearchParams lastAuctionSearch_;
    bool hasAuctionSearch_ = false;  // true after any search (including empty-name browse-all)
    enum class AuctionResultTarget { BROWSE, OWNER, BIDDER };
    AuctionResultTarget pendingAuctionTarget_ = AuctionResultTarget::BROWSE;

    // ---- Trainer state ----
    bool trainerWindowOpen_ = false;
    TrainerListData currentTrainerList_;
    std::vector<TrainerTab> trainerTabs_;

    // ---- Equipment set state ----
    struct EquipmentSet {
        uint64_t setGuid = 0;
        uint32_t setId = 0;
        std::string name;
        std::string iconName;
        uint32_t ignoreSlotMask = 0;
        std::array<uint64_t, 19> itemGuids{};
    };
    std::vector<EquipmentSet> equipmentSets_;
    std::string pendingSaveSetName_;
    std::string pendingSaveSetIcon_;
    std::vector<EquipmentSetInfo> equipmentSetInfo_;

    // ---- Repair cost DBC cache ----
    mutable bool repairDbcLoaded_ = false;
    // DurabilityCosts.dbc: [itemLevel] -> multiplier[29] (weapon subclass 0-20, armor subclass+21)
    mutable std::unordered_map<uint32_t, std::array<uint32_t, 29>> durabilityCosts_;
    // DurabilityQuality.dbc: [id] -> quality_mod float
    mutable std::unordered_map<uint32_t, float> durabilityQuality_;
    void loadRepairDbc() const;
    uint32_t estimateItemRepairCost(uint64_t itemGuid) const;
};

} // namespace game
} // namespace wowee
