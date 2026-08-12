#include "ui/ui_manager.hpp"
#include "ui/interface_fonts.hpp"

#include <algorithm>
#include <filesystem>
#include <chrono>
#include "core/window.hpp"
#include "core/application.hpp"
#include "core/logger.hpp"
#include "auth/auth_handler.hpp"
#include "game/game_handler.hpp"
#include "rendering/vk_context.hpp"
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#ifdef __ANDROID__
#include <SDL.h>
#include <SDL_vulkan.h>
#endif

namespace wowee {
namespace ui {

UIManager::UIManager() {
    // Create screen instances
    authScreen = std::make_unique<AuthScreen>();
    realmScreen = std::make_unique<RealmScreen>();
    characterCreateScreen = std::make_unique<CharacterCreateScreen>();
    characterScreen = std::make_unique<CharacterScreen>();
    gameScreen = std::make_unique<GameScreen>();
}

UIManager::~UIManager() {
    shutdown();
}

bool UIManager::initialize(core::Window* win) {
    // Re-entry (Android activity restart without process death) must tear down
    // the previous SDL/Vulkan backends or ImGui asserts and aborts the process.
    if (imguiInitialized) {
        shutdown();
    }

    window = win;
    LOG_INFO("Initializing UI manager");

    auto* vkCtx = window->getVkContext();
    if (!vkCtx) {
        LOG_ERROR("No Vulkan context available for ImGui initialization");
        return false;
    }

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    if (ImGui::GetCurrentContext() != nullptr) {
        // Leftover context from a prior run that skipped shutdown.
        ImGuiIO& staleIo = ImGui::GetIO();
        if (staleIo.BackendRendererUserData) {
            ImGui_ImplVulkan_Shutdown();
        }
        if (staleIo.BackendPlatformUserData) {
            ImGui_ImplSDL2_Shutdown();
        }
        ImGui::DestroyContext();
    }
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
#ifdef __ANDROID__
    // Prefer touch + pointer: bigger hit boxes, no relative-capture on menus.
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_ShowCursor(SDL_ENABLE);
#endif

    // Setup ImGui style
    ImGui::StyleColorsDark();

    // Customize style for better WoW feel
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
#ifdef __ANDROID__
    // Finger-sized hit targets for buttons / list rows.
    style.TouchExtraPadding = ImVec2(18.0f, 14.0f);
#endif

#ifdef __ANDROID__
    // Landscape tablet: enlarge controls for finger + distance viewing.
    style.WindowPadding = ImVec2(28.0f, 22.0f);
    style.ItemSpacing = ImVec2(14.0f, 12.0f);
    style.ItemInnerSpacing = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(16.0f, 12.0f);
    style.ScrollbarSize = 28.0f;
    style.WindowBorderSize = 0.0f;
    style.ScaleAllSizes(1.45f);
    io.FontGlobalScale = 1.35f;
    LOG_INFO("Android ImGui tablet landscape scale at ",
             window->getWidth(), "x", window->getHeight());
#endif

    // Classic WoW-inspired client colors (launcher stays Obsidian; in-game is WoW).
    ImVec4* colors = style.Colors;
#ifdef __ANDROID__
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.96f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.06f, 0.09f, 0.90f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.14f, 0.96f);
    colors[ImGuiCol_Border] = ImVec4(0.40f, 0.30f, 0.12f, 0.70f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.14f, 0.10f, 0.80f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.22f, 0.10f, 0.80f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.40f, 0.30f, 0.12f, 0.80f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.10f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.15f, 0.06f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.35f, 0.25f, 0.08f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.50f, 0.38f, 0.12f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.20f, 0.06f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.40f, 0.30f, 0.12f, 0.55f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.55f, 0.42f, 0.15f, 0.70f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.65f, 0.50f, 0.18f, 0.85f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.90f, 0.75f, 0.25f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.70f, 0.55f, 0.18f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.90f, 0.75f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.50f, 0.40f, 1.00f);
#else
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.94f);
    // ImGui uses PopupBg for hover tooltips. Keep their text and item details
    // fully legible over the 3D scene.
    colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.06f, 0.09f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.15f, 0.25f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.40f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.30f, 0.50f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.20f, 0.35f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.40f, 0.55f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.30f, 0.50f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.25f, 0.45f, 1.00f);
#endif

    // Initialize ImGui for SDL2 + Vulkan
    ImGui_ImplSDL2_InitForVulkan(window->getSDLWindow());

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_1;
    initInfo.Instance = vkCtx->getInstance();
    initInfo.PhysicalDevice = vkCtx->getPhysicalDevice();
    initInfo.Device = vkCtx->getDevice();
    initInfo.QueueFamily = vkCtx->getGraphicsQueueFamily();
    initInfo.Queue = vkCtx->getGraphicsQueue();
    initInfo.DescriptorPool = vkCtx->getImGuiDescriptorPool();
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = vkCtx->getSwapchainImageCount();
    // The UI renders in the overlay pass, which is single-sampled on purpose:
    // ImGui draws axis-aligned rects and pre-antialiased glyphs, so MSAA buys
    // almost nothing there and costs fill rate at the sample count the scene uses.
    initInfo.PipelineInfoMain.RenderPass = vkCtx->getOverlayRenderPass();
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = [](VkResult err) {
        if (err != VK_SUCCESS)
            LOG_ERROR("ImGui Vulkan error: ", static_cast<int>(err));
    };

    ImGui_ImplVulkan_Init(&initInfo);

    imguiInitialized = true;

    LOG_INFO("UI manager initialized successfully (Vulkan)");
    return true;
}

void UIManager::loadInterfaceFont(const std::string& dataRoot) {
    if (!imguiInitialized) return;
    if (dataRoot.empty()) {
        // Nothing to search is not the same as searching and finding nothing,
        // and both end up in the built-in face.
        LOG_WARNING("No data directory to load interface fonts from — keeping "
                    "the built-in face");
        return;
    }

    namespace fs = std::filesystem;
    std::error_code ec;

    // Extracted data does not agree with itself about case, and this path is
    // reached directly rather than through the asset manager's manifest.
    //
    // Matched a component at a time rather than against a list of spellings.
    // The list only held four, so an install writing Misc/fonts or MISC/FONTS
    // matched none of them, the built-in face was kept, and the only trace was
    // an info line the log does not carry.
    auto childIgnoringCase = [&](const fs::path& base, const std::string& name) {
        const fs::path exact = base / name;
        if (fs::exists(exact, ec)) return exact;
        auto lower = [](std::string v) {
            for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return v;
        };
        const std::string wanted = lower(name);
        for (const auto& entry : fs::directory_iterator(base, ec)) {
            if (lower(entry.path().filename().string()) == wanted) return entry.path();
        }
        return fs::path();
    };

    fs::path fontDir;
    for (const char* rel : { "misc/fonts", "fonts" }) {
        fs::path at(dataRoot);
        for (const auto& part : fs::path(rel)) {
            at = childIgnoringCase(at, part.string());
            if (at.empty()) break;
        }
        if (!at.empty() && fs::is_directory(at, ec)) { fontDir = at; break; }
    }
    if (fontDir.empty()) {
        // Said out loud: the client still runs, in a face that is not the
        // game's, and nothing else reports why.
        LOG_WARNING("No interface fonts under ", dataRoot,
                    " — keeping the built-in face, so text will not look right");
        return;
    }

    // Built at a size above what the interface mostly asks for. A font string
    // carries its own height and is drawn scaled from its face, and scaling
    // down from a larger atlas reads better than up from a smaller.
    constexpr float kAtlasSize = 18.0f;

    // What the client's own panels are drawn at. They were laid out against
    // ImGui's built-in face, which is 13 pixels tall, and FRIZQT at the atlas
    // size above would push text out of buttons sized for it. Close enough to
    // the old metrics to keep those layouts intact, in the game's typeface,
    // which is the point.
#ifdef __ANDROID__
    // Readable tablet type without FontGlobalScale inflation.
    const float kClientSize = 17.0f;
#else
    constexpr float kClientSize = 15.0f;
#endif

    ImGuiIO& io = ImGui::GetIO();

    // Case is not agreed on here either, so look for the file rather than
    // assuming the spelling the manifest happens to use.
    auto resolve = [&](const char* name) {
        fs::path file = fontDir / name;
        if (fs::exists(file, ec)) return file;
        for (const auto& entry : fs::directory_iterator(fontDir, ec)) {
            std::string have = entry.path().filename().string();
            std::transform(have.begin(), have.end(), have.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (have == name) return entry.path();
        }
        return fs::path();
    };

    // FRIZQT at the client's size, first, because ImGui draws with whichever
    // face was added first and that is what everything without an opinion gets
    // — this client's own windows included. The same face is added again below
    // at the atlas size for the interface, which asks for it by name.
    const fs::path frizqt = resolve("frizqt__.ttf");
    if (!frizqt.empty()) {
        if (!io.Fonts->AddFontFromFileTTF(frizqt.string().c_str(), kClientSize)) {
            // Found and refused is a different problem from not found, and
            // reads identically on screen.
            LOG_WARNING("Could not read the interface font at ", frizqt.string(),
                        " — keeping the built-in face");
            io.Fonts->AddFontDefault();
        }
    } else {
        LOG_WARNING("No frizqt__.ttf in ", fontDir.string(),
                    " — keeping the built-in face");
        io.Fonts->AddFontDefault();
    }

    // The faces FrameXML's font objects name: body text in FRIZQT, headings in
    // MORPHEUS, damage in SKURRI, condensed numbers in ARIALN.
    const char* faces[] = {
        "frizqt__.ttf", "morpheus.ttf", "skurri.ttf", "arialn.ttf", "friends.ttf"
    };
    int loaded = 0;
    for (const char* name : faces) {
        const fs::path file = resolve(name);
        if (file.empty()) continue;
        if (ImFont* f = io.Fonts->AddFontFromFileTTF(file.string().c_str(), kAtlasSize)) {
            registerInterfaceFace(name, f);
            ++loaded;
        }
    }
    LOG_WARNING("Interface fonts loaded: ", loaded, " of 5 from ", fontDir.string());
}

void UIManager::shutdown() {
    if (imguiInitialized) {
        auto* vkCtx = window ? window->getVkContext() : nullptr;
        if (vkCtx) {
            vkDeviceWaitIdle(vkCtx->getDevice());
        }
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized = false;
    }
    LOG_INFO("UI manager shutdown");
}

void UIManager::update([[maybe_unused]] float deltaTime) {
    if (!imguiInitialized) return;

    // Start ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
#ifdef __ANDROID__
    // SDL_GetWindowSize can stay portrait (1200x1920) while the Vulkan
    // drawable/swapchain is landscape (1920x1200). ImGui then lays out a
    // tall phone column that renders as a thin sideways strip. Always drive
    // DisplaySize from the drawable and keep framebuffer scale at 1:1.
    if (window && window->getSDLWindow()) {
        int dw = 0;
        int dh = 0;
        SDL_Vulkan_GetDrawableSize(window->getSDLWindow(), &dw, &dh);
        if (dw > 0 && dh > 0) {
            if (dh > dw) {
                std::swap(dw, dh);
            }
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(static_cast<float>(dw), static_cast<float>(dh));
            io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        }
    }
#endif
    ImGui::NewFrame();
}

void UIManager::render(core::AppState appState, auth::AuthHandler* authHandler, game::GameHandler* gameHandler) {
    if (!imguiInitialized) return;

    // Two ~150-200ms spikes land here every launch, before login. Decoding the
    // auth background off the main thread did not move them, so report which
    // application state was being drawn when one happens — that narrows it to a
    // screen before anyone goes looking inside one.
    const auto uiRenderStart = std::chrono::steady_clock::now();
    struct StateReport {
        std::chrono::steady_clock::time_point start;
        core::AppState state;
        ~StateReport() {
            const float ms = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            if (ms > 50.0f) {
                LOG_WARNING("SLOW UI screen render: ", ms, "ms in appState=",
                            static_cast<int>(state));
            }
        }
    } stateReport{uiRenderStart, appState};

    // Render appropriate screen based on application state
    switch (appState) {
        case core::AppState::AUTHENTICATION:
            if (authHandler) {
                authScreen->render(*authHandler);
            }
            break;

        case core::AppState::REALM_SELECTION:
            authScreen->stopLoginMusic();
            if (authHandler) {
                realmScreen->render(*authHandler);
            }
            break;

        case core::AppState::CHARACTER_CREATION:
            authScreen->stopLoginMusic();
            if (gameHandler) {
                characterCreateScreen->render(*gameHandler);
            }
            break;

        case core::AppState::CHARACTER_SELECTION:
            authScreen->stopLoginMusic();
            if (gameHandler) {
                characterScreen->render(*gameHandler);
            }
            break;

        case core::AppState::IN_GAME:
            authScreen->stopLoginMusic();
            if (gameHandler) {
                gameScreen->render(*gameHandler);
            }
            break;

        case core::AppState::DISCONNECTED:
            authScreen->stopLoginMusic();
            ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 200,
                                           ImGui::GetIO().DisplaySize.y * 0.5f - 75),
                                    ImGuiCond_Always);
            ImGui::Begin("Disconnected", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
            ImGui::TextWrapped("You have been disconnected from the server.");
            ImGui::Spacing();
            if (ImGui::Button("Return to Login", ImVec2(-1, 0))) {
                // Will be handled by application
            }
            ImGui::End();
            break;
    }

    // Finalize ImGui draw data (actual rendering happens in the command buffer)
    ImGui::Render();

#ifdef __ANDROID__
    // Soft keyboard for ImGui InputText (character name, etc.).
    // Edge-toggle only — leaving text input always on keeps the IME up and can
    // steal focus from the SDL surface. Relative mouse / touch are unaffected
    // while WantTextInput is false.
    {
        static bool wasWantTextInput = false;
        const bool want = ImGui::GetIO().WantTextInput;
        if (want && !wasWantTextInput) {
            SDL_StartTextInput();
        } else if (!want && wasWantTextInput) {
            SDL_StopTextInput();
        }
        wasWantTextInput = want;
    }
#endif
}

void UIManager::processEvent(const SDL_Event& event) {
    if (imguiInitialized) {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }
}

} // namespace ui
} // namespace wowee
