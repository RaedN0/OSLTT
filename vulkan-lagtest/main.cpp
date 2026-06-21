#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <chrono>
#include <cstring>
#include <algorithm>

#include <cstdlib>
#include <thread>

static bool g_buttonPressed = false;
static bool g_keyPressed = false;

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (action == GLFW_PRESS) g_buttonPressed = true;
    else if (action == GLFW_RELEASE) g_buttonPressed = false;
}

void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    // Position is polled directly via glfwGetCursorPos; callback not needed
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) g_keyPressed = true;
    else if (action == GLFW_RELEASE) g_keyPressed = false;
}

static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

// ─── Swapchain bundle ───
struct Swapchain {
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkFormat format{};
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkCommandBuffer> commandBuffers;
};

static void destroySwapchain(VkDevice device, Swapchain& sc, VkCommandPool commandPool) {
    vkDeviceWaitIdle(device);
    if (sc.commandBuffers.size() > 0 && commandPool != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, commandPool,
            static_cast<uint32_t>(sc.commandBuffers.size()), sc.commandBuffers.data());
        sc.commandBuffers.clear();
    }
    for (auto& fb : sc.framebuffers) if (fb) vkDestroyFramebuffer(device, fb, nullptr);
    sc.framebuffers.clear();
    for (auto& iv : sc.imageViews) if (iv) vkDestroyImageView(device, iv, nullptr);
    sc.imageViews.clear();
    if (sc.handle) vkDestroySwapchainKHR(device, sc.handle, nullptr);
    sc.handle = VK_NULL_HANDLE;
}

static Swapchain createSwapchain(
    VkPhysicalDevice physDev, VkDevice device, VkSurfaceKHR surface,
    GLFWwindow* window, VkRenderPass renderPass, VkCommandPool commandPool)
{
    Swapchain sc{};

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDev, surface, &caps);

    // Determine extent
    sc.extent = caps.currentExtent;
    if (sc.extent.width == 0xFFFFFFFF || sc.extent.height == 0xFFFFFFFF ||
        sc.extent.width == 0 || sc.extent.height == 0)
    {
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        sc.extent.width = static_cast<uint32_t>(w);
        sc.extent.height = static_cast<uint32_t>(h);
    }
    sc.extent.width = std::max(caps.minImageExtent.width,
                               std::min(caps.maxImageExtent.width, sc.extent.width));
    sc.extent.height = std::max(caps.minImageExtent.height,
                                std::min(caps.maxImageExtent.height, sc.extent.height));

    // Pick format
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &formatCount, formats.data());
    VkSurfaceFormatKHR sf = formats.empty()
        ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
        : formats[0];
    sc.format = sf.format;

    // Pick present mode — prefer IMMEDIATE (uncapped), fallback to MAILBOX, then FIFO
    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> pms(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &pmCount, pms.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // guaranteed
    for (auto& pm : pms) { if (pm == VK_PRESENT_MODE_IMMEDIATE_KHR) { presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR; break; } }
    if (presentMode != VK_PRESENT_MODE_IMMEDIATE_KHR) {
        for (auto& pm : pms) { if (pm == VK_PRESENT_MODE_MAILBOX_KHR) { presentMode = VK_PRESENT_MODE_MAILBOX_KHR; break; } }
    }

    // Composite alpha
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
            compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
            compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }

    uint32_t minImageCount = std::max(2u, caps.minImageCount);
    if (caps.maxImageCount > 0 && minImageCount > caps.maxImageCount)
        minImageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface;
    ci.minImageCount = minImageCount;
    ci.imageFormat = sf.format;
    ci.imageColorSpace = sf.colorSpace;
    ci.imageExtent = sc.extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = compositeAlpha;
    ci.presentMode = presentMode;
    ci.clipped = VK_TRUE;

    VkResult r = vkCreateSwapchainKHR(device, &ci, nullptr, &sc.handle);
    if (r != VK_SUCCESS) {
        std::cerr << "createSwapchain: vkCreateSwapchainKHR returned " << r << std::endl;
        std::cerr << "  extent=" << sc.extent.width << "x" << sc.extent.height
                  << " format=" << sf.format << " presentMode=" << presentMode
                  << " minImageCount=" << minImageCount << std::endl;
        return sc;
    }

    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(device, sc.handle, &imageCount, nullptr);
    sc.images.resize(imageCount);
    vkGetSwapchainImagesKHR(device, sc.handle, &imageCount, sc.images.data());

    sc.imageViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = sc.images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = sc.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &vi, nullptr, &sc.imageViews[i]);
    }

    sc.framebuffers.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = renderPass;
        fi.attachmentCount = 1;
        fi.pAttachments = &sc.imageViews[i];
        fi.width = sc.extent.width;
        fi.height = sc.extent.height;
        fi.layers = 1;
        vkCreateFramebuffer(device, &fi, nullptr, &sc.framebuffers[i]);
    }

    sc.commandBuffers.resize(imageCount);
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = imageCount;
    vkAllocateCommandBuffers(device, &ai, sc.commandBuffers.data());

    std::cerr << "=== SWAPCHAIN (re)created ===" << std::endl;
    std::cerr << "  extent=" << sc.extent.width << "x" << sc.extent.height << std::endl;
    std::cerr << "  format=" << sf.format << " colorSpace=" << sf.colorSpace << std::endl;
    std::cerr << "  presentMode=" << presentMode << " images=" << imageCount << std::endl;

    return sc;
}

int main(int argc, char** argv) {
    // Parse FPS limit from command line: ./lagtest [fps_limit]
    // 0 or omitted = uncapped (only limited by hardware)
    int fpsLimit = 0;
    if (argc >= 2) {
        fpsLimit = std::atoi(argv[1]);
        if (fpsLimit < 0) fpsLimit = 0;
    }
    std::cerr << "FPS limit: " << (fpsLimit > 0 ? std::to_string(fpsLimit) : "uncapped") << std::endl;

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Lag Test", nullptr, nullptr);
    if (!window) { std::cerr << "Failed to create window" << std::endl; return 1; }

    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetKeyCallback(window, keyCallback);

    // Vulkan instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Lag Test";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> exts(glfwExts, glfwExts + glfwExtCount);

    VkInstanceCreateInfo instCi{};
    instCi.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instCi.pApplicationInfo = &appInfo;
    instCi.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    instCi.ppEnabledExtensionNames = exts.data();

    VkInstance instance;
    if (vkCreateInstance(&instCi, nullptr, &instance) != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan instance" << std::endl; return 1;
    }

    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        std::cerr << "Failed to create window surface" << std::endl; return 1;
    }

    // Pick physical device + queue family
    uint32_t devCount = 0;
    vkEnumeratePhysicalDevices(instance, &devCount, nullptr);
    std::vector<VkPhysicalDevice> devices(devCount);
    vkEnumeratePhysicalDevices(instance, &devCount, devices.data());
    VkPhysicalDevice physicalDevice = devices[0];

    uint32_t qfIndex = 0, qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, qfs.data());
    for (uint32_t i = 0; i < qfCount; i++) {
        VkBool32 present = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &present);
        if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { qfIndex = i; break; }
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo qCi{};
    qCi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qCi.queueFamilyIndex = qfIndex;
    qCi.queueCount = 1;
    qCi.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures devFeatures{};
    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo devCi{};
    devCi.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devCi.pQueueCreateInfos = &qCi;
    devCi.queueCreateInfoCount = 1;
    devCi.pEnabledFeatures = &devFeatures;
    devCi.enabledExtensionCount = 1;
    devCi.ppEnabledExtensionNames = devExts;

    VkDevice device;
    if (vkCreateDevice(physicalDevice, &devCi, nullptr, &device) != VK_SUCCESS) {
        std::cerr << "Failed to create logical device" << std::endl; return 1;
    }

    VkQueue graphicsQueue;
    vkGetDeviceQueue(device, qfIndex, 0, &graphicsQueue);

    // Let compositor configure surface
    for (int i = 0; i < 20; ++i) glfwPollEvents();

    // Render pass (format-agnostic until swapchain exists — recreate if format changes)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    // Create a placeholder render pass; we'll recreate after first swapchain
    VkFormat currentFormat = VK_FORMAT_B8G8R8A8_UNORM;
    colorAttachment.format = currentFormat;

    VkRenderPassCreateInfo rpCi{};
    rpCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpCi.attachmentCount = 1;
    rpCi.pAttachments = &colorAttachment;
    rpCi.subpassCount = 1;
    rpCi.pSubpasses = &subpass;

    VkRenderPass renderPass;
    vkCreateRenderPass(device, &rpCi, nullptr, &renderPass);

    // Command pool
    VkCommandPoolCreateInfo poolCi{};
    poolCi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCi.queueFamilyIndex = qfIndex;
    VkCommandPool commandPool;
    vkCreateCommandPool(device, &poolCi, nullptr, &commandPool);

    // Shaders
    auto vertCode = readFile("vert.spv");
    auto fragCode = readFile("frag.spv");

    VkShaderModuleCreateInfo vSi{};
    vSi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vSi.codeSize = vertCode.size();
    vSi.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
    VkShaderModule vertModule;
    vkCreateShaderModule(device, &vSi, nullptr, &vertModule);

    VkShaderModuleCreateInfo fSi{};
    fSi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fSi.codeSize = fragCode.size();
    fSi.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
    VkShaderModule fragModule;
    vkCreateShaderModule(device, &fSi, nullptr, &fragModule);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // DYNAMIC viewport/scissor so pipeline doesn't need recreation on resize
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blending{};
    blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blending.attachmentCount = 1;
    blending.pAttachments = &blendAtt;

    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dynStates;

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(float) * 4;

    VkPipelineLayoutCreateInfo plCi{};
    plCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCi.pushConstantRangeCount = 1;
    plCi.pPushConstantRanges = &pcRange;

    VkPipelineLayout pipelineLayout;
    vkCreatePipelineLayout(device, &plCi, nullptr, &pipelineLayout);

    VkGraphicsPipelineCreateInfo pipeCi{};
    pipeCi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeCi.stageCount = 2;
    pipeCi.pStages = stages;
    pipeCi.pVertexInputState = &vertexInput;
    pipeCi.pInputAssemblyState = &inputAssembly;
    pipeCi.pViewportState = &viewportState;
    pipeCi.pRasterizationState = &rasterizer;
    pipeCi.pMultisampleState = &multisampling;
    pipeCi.pColorBlendState = &blending;
    pipeCi.pDynamicState = &dynState;
    pipeCi.layout = pipelineLayout;
    pipeCi.renderPass = renderPass;
    pipeCi.subpass = 0;

    VkPipeline pipeline;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeCi, nullptr, &pipeline);

    // NOTE: shader modules kept alive — needed if pipeline is recreated on format change
    // Sync objects
    VkSemaphoreCreateInfo semCi{};
    VkFenceCreateInfo fenceCi{};
    fenceCi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphore imageAvailableSemaphore, renderFinishedSemaphore;
    VkFence inFlightFence;
    vkCreateSemaphore(device, &semCi, nullptr, &imageAvailableSemaphore);
    vkCreateSemaphore(device, &semCi, nullptr, &renderFinishedSemaphore);
    vkCreateFence(device, &fenceCi, nullptr, &inFlightFence);

    // First swapchain
    Swapchain sc = createSwapchain(physicalDevice, device, surface, window, renderPass, commandPool);

    // ─── Render loop ───
    bool currentWhite = false;
    auto holdUntil = std::chrono::steady_clock::now();
    double prevX, prevY;
    glfwGetCursorPos(window, &prevX, &prevY);

    auto fpsStart = std::chrono::steady_clock::now();
    int fpsFrames = 0;
    int presentedCount = 0;
    int acquireFailCount = 0;
    int recreateCount = 0;
    VkResult lastAcquire = VK_SUCCESS, lastPresent = VK_SUCCESS;
    auto frameStart = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double curX, curY;
        glfwGetCursorPos(window, &curX, &curY);
        bool movedRight = (curX > prevX);
        bool movedLeft = (curX < prevX);
        prevX = curX;
        prevY = curY;

        auto now = std::chrono::steady_clock::now();

        if (movedRight) {
            holdUntil = now + std::chrono::microseconds(2000);
            if (!currentWhite) {
                currentWhite = true;
                auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                std::cerr << "WHITE " << us << std::endl;
            }
        }
        if (movedLeft) {
            holdUntil = now;  // expire immediately
            if (currentWhite) {
                currentWhite = false;
                auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                std::cerr << "BLACK " << us << std::endl;
            }
        }

        // Auto-revert to black after hold expires
        if (currentWhite && now >= holdUntil) {
            currentWhite = false;
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            std::cerr << "BLACK " << us << std::endl;
        }

        fpsFrames++;
        if (now - fpsStart >= std::chrono::seconds(1)) {
            std::cerr << "FPS: " << fpsFrames << " white=" << currentWhite
                      << " acquire=" << lastAcquire << " present=" << lastPresent
                      << " presented=" << presentedCount << " acqFails=" << acquireFailCount
                      << " recreates=" << recreateCount
                      << " pos=" << curX << "," << curY << std::endl;
            fpsFrames = 0;
            presentedCount = 0;
            acquireFailCount = 0;
            recreateCount = 0;
            fpsStart = now;
        }

        // Wait for previous frame
        // Software frame rate limiter
        if (fpsLimit > 0) {
            auto targetFrameTime = std::chrono::microseconds(1000000 / fpsLimit);
            auto elapsed = std::chrono::steady_clock::now() - frameStart;
            if (elapsed < targetFrameTime) {
                std::this_thread::sleep_for(targetFrameTime - elapsed);
            }
        }

        VkResult waitResult = vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, 1000000000);
        if (waitResult == VK_TIMEOUT) {
            std::cerr << "WARNING: vkWaitForFences timed out" << std::endl;
            continue;
        }

        // Acquire next image
        uint32_t imageIndex;
        VkResult acquireResult = vkAcquireNextImageKHR(
            device, sc.handle, 1000000000, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
        lastAcquire = acquireResult;

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            // Surface changed — recreate swapchain
            acquireFailCount++;
            destroySwapchain(device, sc, commandPool);

            // Update render pass format if needed
            // Re-query surface format
            uint32_t fmtCount = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &fmtCount, nullptr);
            std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &fmtCount, fmts.data());
            VkFormat newFormat = fmts.empty() ? VK_FORMAT_B8G8R8A8_UNORM : fmts[0].format;
            if (newFormat != currentFormat) {
                // Recreate render pass with new format
                vkDestroyRenderPass(device, renderPass, nullptr);
                colorAttachment.format = newFormat;
                rpCi.pAttachments = &colorAttachment;
                vkCreateRenderPass(device, &rpCi, nullptr, &renderPass);
                currentFormat = newFormat;

                // Recreate pipeline (depends on render pass)
                vkDestroyPipeline(device, pipeline, nullptr);
                pipeCi.renderPass = renderPass;
                vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeCi, nullptr, &pipeline);
            }

            sc = createSwapchain(physicalDevice, device, surface, window, renderPass, commandPool);
            recreateCount++;
            continue;
        }
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            acquireFailCount++;
            continue;
        }

        vkResetFences(device, 1, &inFlightFence);

        // Record command buffer
        vkResetCommandBuffer(sc.commandBuffers[imageIndex], 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(sc.commandBuffers[imageIndex], &beginInfo);

        // Dynamic viewport/scissor
        VkViewport viewport{};
        viewport.width = static_cast<float>(sc.extent.width);
        viewport.height = static_cast<float>(sc.extent.height);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(sc.commandBuffers[imageIndex], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = sc.extent;
        vkCmdSetScissor(sc.commandBuffers[imageIndex], 0, 1, &scissor);

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = sc.framebuffers[imageIndex];
        rpBegin.renderArea.extent = sc.extent;

        VkClearValue clearValue{};
        if (currentWhite) {
            clearValue.color.float32[0] = 1.0f;
            clearValue.color.float32[1] = 1.0f;
            clearValue.color.float32[2] = 1.0f;
            clearValue.color.float32[3] = 1.0f;
        }
        rpBegin.pClearValues = &clearValue;
        rpBegin.clearValueCount = 1;

        vkCmdBeginRenderPass(sc.commandBuffers[imageIndex], &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(sc.commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        float color[4] = {currentWhite ? 1.0f : 0.0f,
                         currentWhite ? 1.0f : 0.0f,
                         currentWhite ? 1.0f : 0.0f, 1.0f};
        vkCmdPushConstants(sc.commandBuffers[imageIndex], pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(color), color);
        vkCmdDraw(sc.commandBuffers[imageIndex], 3, 1, 0, 0);
        vkCmdEndRenderPass(sc.commandBuffers[imageIndex]);
        vkEndCommandBuffer(sc.commandBuffers[imageIndex]);

        // Submit
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &sc.commandBuffers[imageIndex];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinishedSemaphore;
        VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence);
        if (submitResult != VK_SUCCESS) {
            std::cerr << "WARNING: vkQueueSubmit returned " << submitResult << std::endl;
            continue;
        }

        // Present
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &sc.handle;
        presentInfo.pImageIndices = &imageIndex;
        VkResult presentResult = vkQueuePresentKHR(graphicsQueue, &presentInfo);
        lastPresent = presentResult;

        if (presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR) {
            presentedCount++;
        }
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
            // Will recreate on next acquire
        } else if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR) {
            std::cerr << "WARNING: vkQueuePresentKHR returned " << presentResult << std::endl;
        }

        frameStart = std::chrono::steady_clock::now();
    }

    // Cleanup
    vkDeviceWaitIdle(device);
    destroySwapchain(device, sc, commandPool);
    vkDestroyFence(device, inFlightFence, nullptr);
    vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
    vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
