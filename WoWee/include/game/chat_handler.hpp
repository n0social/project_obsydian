#pragma once

#include "game/world_packets.hpp"
#include "game/opcode_table.hpp"
#include "game/handler_types.hpp"
#include "network/packet.hpp"
#include <deque>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wowee {
namespace game {

class GameHandler;

class ChatHandler {
public:
    using PacketHandler = std::function<void(network::Packet&)>;
    using DispatchTable = std::unordered_map<LogicalOpcode, PacketHandler>;

    explicit ChatHandler(GameHandler& owner);

    void registerOpcodes(DispatchTable& table);

    // --- Public API (delegated from GameHandler) ---
    void sendChatMessage(ChatType type, const std::string& message, const std::string& target = "");
    void sendAddonMessage(ChatType type, const std::string& message, const std::string& target = "");
    void sendTextEmote(uint32_t textEmoteId, uint64_t targetGuid = 0);
    void joinChannel(const std::string& channelName, const std::string& password = "");
    void leaveChannel(const std::string& channelName);
    std::string getChannelByIndex(int index) const;
    int getChannelIndex(const std::string& channelName) const;
    const std::vector<std::string>& getJoinedChannels() const { return joinedChannels_; }
    void autoJoinDefaultChannels();
    void addLocalChatMessage(const MessageChatData& msg);
    void addSystemChatMessage(const std::string& message);
    void toggleAfk(const std::string& message);
    void toggleDnd(const std::string& message);
    void replyToLastWhisper(const std::string& message);

    // ---- Methods moved from GameHandler ----
    void submitGmTicket(const std::string& text);
    void handleMotd(network::Packet& packet);
    void handleNotification(network::Packet& packet);

    // --- State accessors ---
    std::deque<MessageChatData>& getChatHistory() { return chatHistory_; }
    const std::deque<MessageChatData>& getChatHistory() const { return chatHistory_; }
    size_t getMaxChatHistory() const { return maxChatHistory_; }
    void setMaxChatHistory(size_t n) { maxChatHistory_ = n; }

    // Chat auto-join settings (aliased from handler_types.hpp)
    using ChatAutoJoin = game::ChatAutoJoin;
    ChatAutoJoin chatAutoJoin;

private:
    // --- Packet handlers ---
    void handleMessageChat(network::Packet& packet);
    void handleTextEmote(network::Packet& packet);
    void handleChannelNotify(network::Packet& packet);
    void handleChannelList(network::Packet& packet);
    void initializeChatLog();
    void logChatMessage(const MessageChatData& msg, const char* source);

    GameHandler& owner_;

    // --- State ---
    std::deque<MessageChatData> chatHistory_;
    size_t maxChatHistory_ = 100;
    uint64_t chatUidCounter_ = 0;  // monotonic uid for MessageChatData::uid
    std::vector<std::string> joinedChannels_;
    bool chatLogEnabled_ = false;
    bool chatLogInitialized_ = false;
    std::string chatLogPath_;
    std::ofstream chatLogStream_;

    // Track senders we've already auto-replied to (AFK/DND) this session
    // to prevent infinite reply loops. Cleared when AFK/DND is toggled off.
    std::unordered_set<std::string> afkAutoRepliedSenders_;
};

} // namespace game
} // namespace wowee
