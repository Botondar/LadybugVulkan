#include <cstdio>
#include <cassert>
#include <cassert>

#include <vulkan/vulkan.h>
#include <Windows.h>
#include <vulkan/vulkan_win32.h>

#include <cinttypes>
#include <vector>
#include <array>
#include <cstring>

#define ArrayCount(a) (sizeof((a)) / sizeof((a)[0]))

#undef min
#undef max

template<typename T>
T Clamp(T Val, T Min, T Max)
{
    return std::max(Min, std::min(Val, Max));
}

struct SVulkanVersion
{
    uint32_t ApiVersion;
    uint32_t MajorVersion;
    uint32_t MinorVersion;
    uint32_t PatchVersion;
};

inline SVulkanVersion VulkanExtractVersion(uint32_t ApiVersion)
{
    SVulkanVersion Version = {};
    Version.ApiVersion = ApiVersion;
    Version.MajorVersion = VK_VERSION_MAJOR(ApiVersion);
    Version.MinorVersion = VK_VERSION_MINOR(ApiVersion);
    Version.PatchVersion = VK_VERSION_PATCH(ApiVersion);
    return Version;
}

struct SVulkanLayer
{
    VkLayerProperties Properties;
    std::vector<VkExtensionProperties> Extensions;
};

struct SVulkanPhysicalDevice
{
    VkPhysicalDevice Device;

    SVulkanVersion Version;

    VkPhysicalDeviceProperties Properties;
    VkPhysicalDeviceFeatures Features;
    VkPhysicalDeviceMemoryProperties MemoryProperties;
    std::vector<SVulkanLayer> Layers;
    std::vector<VkExtensionProperties> Extensions;
    std::vector<VkQueueFamilyProperties> QueueFamilies;
};

struct SVulkanState
{
    SVulkanVersion Version;

    std::vector<SVulkanLayer> InstanceLayers;
    std::vector<VkExtensionProperties> InstanceExtensions;

    VkInstance Instance;

    std::vector<SVulkanPhysicalDevice> PhysicalDevices;

    VkPhysicalDevice SelectedDevice = VK_NULL_HANDLE;
    uint32_t SelectedDeviceQueueFamilyIndex = 0;

    VkSurfaceKHR Surface;
    VkExtent2D SurfaceExtent;
    VkFormat SurfaceFormat;
    VkColorSpaceKHR SurfaceColorSpace;

    VkPresentModeKHR SurfacePresentMode;
    VkSurfaceCapabilitiesKHR SurfaceCapabilities;

    VkDevice Device;
    VkQueue Queue;

    VkSwapchainKHR Swapchain;
    std::vector<VkImage> SwapchainImages;
    std::vector<VkImageView> SwapchainImageViews;

    VkShaderModule Shader;

    VkRenderPass RenderPass;
    VkPipeline Pipeline;

    std::vector<VkFramebuffer> Framebuffers;

    VkCommandPool CommandPool;
    std::vector<VkCommandBuffer> CommandBuffers;
};


VkBool32 DebugCallback(VkDebugReportFlagsEXT Flags, VkDebugReportObjectTypeEXT ObjectType, uint64_t Object, size_t Location,
                       int32_t MessageCode, const char* LayerPrefix, const char* Message, void* pUserData)
{
    printf("%s\n", Message);
    return VK_FALSE;
}

struct SBuffer
{
    uint32_t Size;
    void* Data;
};

void ReleaseBuffer(SBuffer* Buffer)
{
    delete[] Buffer->Data;
    Buffer->Size = 0;
    Buffer->Data = nullptr;
}

SBuffer win32LoadFile(const char* Path)
{
    SBuffer Buffer = {};

    HANDLE File = CreateFile(Path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if(File && File != INVALID_HANDLE_VALUE)
    {
        Buffer.Size = GetFileSize(File, nullptr);
        Buffer.Data = new uint8_t[Buffer.Size];

        DWORD BytesRead;
        ReadFile(File, Buffer.Data, Buffer.Size, &BytesRead, nullptr);

        assert(BytesRead == Buffer.Size);
        CloseHandle(File);
    }
    else
    {
        assert(!"Invalid file");
    }
    return Buffer;
}

LRESULT CALLBACK win32MainWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    LRESULT Result = 0;

    switch(Message)
    {
        case WM_CLOSE:
            PostQuitMessage(0);
            break;
        default:
            Result = DefWindowProc(Window, Message, WParam, LParam);
            break;
    }

    return Result;
}

HWND win32OpenWindow(const char* Title, int32_t Width, int32_t Height)
{
    static uint32_t _CallCount = 0;
    assert(_CallCount++ == 0);

    WNDCLASS WindowClass = {};
    WindowClass.style = CS_OWNDC;
    WindowClass.lpfnWndProc = &win32MainWindowProc;
    WindowClass.hInstance = nullptr;
    WindowClass.lpszClassName = "vkclass";

    assert(RegisterClass(&WindowClass));
    
    int32_t MonitorWidth, MonitorHeight;
    {
        POINT P = { 0, 0 };
        HMONITOR Monitor = MonitorFromPoint(P, MONITOR_DEFAULTTOPRIMARY);

        MONITORINFO MonitorInfo = { sizeof(MONITORINFO) };
        GetMonitorInfo(Monitor, &MonitorInfo);

        MonitorWidth = MonitorInfo.rcMonitor.right - MonitorInfo.rcMonitor.left;
        MonitorHeight = MonitorInfo.rcMonitor.bottom - MonitorInfo.rcMonitor.top;
    }

    RECT WindowRect = {};
    WindowRect.left = (MonitorWidth - Width) / 2;
    WindowRect.right = WindowRect.left + Width;
    WindowRect.top = (MonitorHeight - Height) / 2;
    WindowRect.bottom = WindowRect.top + Height;

    DWORD WindowStyle = WS_OVERLAPPEDWINDOW & (~WS_MAXIMIZEBOX) & (~WS_THICKFRAME);
    AdjustWindowRect(&WindowRect, WindowStyle, FALSE);

    HWND Window = CreateWindow(WindowClass.lpszClassName, Title, WindowStyle,
                               WindowRect.left, WindowRect.top,
                               WindowRect.right - WindowRect.left,
                               WindowRect.bottom - WindowRect.top,
                               nullptr, nullptr, nullptr, nullptr);

    assert(Window);

    ShowWindow(Window, SW_SHOW);

    return Window;
}

int main(int ArgCount, char** Args)
{
    constexpr uint32_t Width = 800;
    constexpr uint32_t Height = 600;

    HINSTANCE Instance = GetModuleHandle(nullptr);

    HWND Window = win32OpenWindow("vktest", Width, Height);

    VkResult Result = VK_SUCCESS;

    SVulkanState VulkanState = {};

    // Enumerate version
    {
        uint32_t ApiVersion;
        vkEnumerateInstanceVersion(&ApiVersion);

        VulkanState.Version = VulkanExtractVersion(ApiVersion);

        assert(VulkanState.Version.MajorVersion >= 1 && VulkanState.Version.MinorVersion >= 1);
    }

    // Enumerate instance layers and extensions
    {
        // Global extensions
        {
            uint32_t ExtensionCount;
            vkEnumerateInstanceExtensionProperties(nullptr, &ExtensionCount, nullptr);
            VulkanState.InstanceExtensions.resize(ExtensionCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &ExtensionCount, VulkanState.InstanceExtensions.data());
        }

        // Layers and layer extensions
        uint32_t LayerCount;
        vkEnumerateInstanceLayerProperties(&LayerCount, nullptr);
        std::vector<VkLayerProperties> LayerProperties(LayerCount);
        vkEnumerateInstanceLayerProperties(&LayerCount, LayerProperties.data());

        // SoA to AoS conversion
        VulkanState.InstanceLayers.resize(LayerCount);
        for(uint32_t LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
        {
            SVulkanLayer& Layer = VulkanState.InstanceLayers[LayerIndex];
            Layer.Properties = LayerProperties[LayerIndex];

            uint32_t ExtensionCount;
            vkEnumerateInstanceExtensionProperties(Layer.Properties.layerName, &ExtensionCount, nullptr);
            Layer.Extensions.resize(ExtensionCount);
            vkEnumerateInstanceExtensionProperties(Layer.Properties.layerName, &ExtensionCount, Layer.Extensions.data());
        }
    }

    // Create instance
    {
        VkApplicationInfo AppInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
        AppInfo.pNext = nullptr;
        AppInfo.pApplicationName = "Ladybug";
        AppInfo.applicationVersion = 1;
        AppInfo.pEngineName = "LadybugEngine";
        AppInfo.engineVersion = 1;
        AppInfo.apiVersion = VK_API_VERSION_1_1;


        const char* const Extensions[] =
        {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
            VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
        };

        constexpr uint32_t ExtensionCount = ArrayCount(Extensions);

        // Find required extensions
        std::array<bool, ExtensionCount> ExtensionsFound = {};
        for(const VkExtensionProperties& Extension : VulkanState.InstanceExtensions)
        {
            for(uint32_t i = 0; i < ExtensionCount; ++i)
            {
                if(strcmp(Extension.extensionName, Extensions[i]) == 0)
                {
                    ExtensionsFound[i] = true;
                }
            }
        }

        if(std::find(ExtensionsFound.begin(), ExtensionsFound.end(), false) != ExtensionsFound.end())
        {
            printf("Unavailable extension\n");
            return -1;
        }

        const char* const Layers[] =
        {
            "VK_LAYER_KHRONOS_validation",
        };

        constexpr uint32_t LayerCount = ArrayCount(Layers);

        // Find required layers
        std::array<bool, LayerCount> LayersFound = {};
        for(const SVulkanLayer& Layer : VulkanState.InstanceLayers)
        {
            for(uint32_t i = 0; i < LayerCount; ++i)
            {
                if(strcmp(Layer.Properties.layerName, Layers[i]) == 0)
                {
                    LayersFound[i] = true;
                }
            }
        }

        if(std::find(LayersFound.begin(), LayersFound.end(), false) != LayersFound.end())
        {
            printf("Unavailable layer\n");
            return -1;
        }

        VkInstanceCreateInfo InstanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        InstanceCreateInfo.pNext = nullptr;
        InstanceCreateInfo.flags = 0;
        InstanceCreateInfo.pApplicationInfo = &AppInfo;
        InstanceCreateInfo.enabledExtensionCount = ExtensionCount;
        InstanceCreateInfo.ppEnabledExtensionNames = Extensions;
        InstanceCreateInfo.enabledLayerCount = LayerCount;
        InstanceCreateInfo.ppEnabledLayerNames = Layers;

        Result = vkCreateInstance(&InstanceCreateInfo, nullptr, &VulkanState.Instance);
        assert(Result == VK_SUCCESS);
    }

    // Initialize debug callback
    PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallback = VK_NULL_HANDLE;
    vkCreateDebugReportCallback = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(VulkanState.Instance, "vkCreateDebugReportCallbackEXT");
    if(vkCreateDebugReportCallback)
    {

        VkDebugReportCallbackCreateInfoEXT DebugReportCallbackCreateInfo = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT };
        DebugReportCallbackCreateInfo.pNext = nullptr;
        DebugReportCallbackCreateInfo.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT|VK_DEBUG_REPORT_ERROR_BIT_EXT|VK_DEBUG_REPORT_DEBUG_BIT_EXT;
        DebugReportCallbackCreateInfo.pfnCallback = &DebugCallback;
        DebugReportCallbackCreateInfo.pUserData = nullptr;

        VkDebugReportCallbackEXT DebugCallbackObj;
        vkCreateDebugReportCallback(VulkanState.Instance, &DebugReportCallbackCreateInfo, nullptr, &DebugCallbackObj);
    }

    // Enumerate physical devices
    {
        uint32_t PhysicalDeviceCount;
        vkEnumeratePhysicalDevices(VulkanState.Instance, &PhysicalDeviceCount, nullptr);
        std::vector<VkPhysicalDevice> PhysicalDevices(PhysicalDeviceCount);
        vkEnumeratePhysicalDevices(VulkanState.Instance, &PhysicalDeviceCount, PhysicalDevices.data());

        VulkanState.PhysicalDevices.resize(PhysicalDeviceCount);
        for(uint32_t DeviceIndex = 0; DeviceIndex < PhysicalDeviceCount; ++DeviceIndex)
        {
            SVulkanPhysicalDevice& Device = VulkanState.PhysicalDevices[DeviceIndex];
            Device.Device = PhysicalDevices[DeviceIndex];

            // Get device properties
            vkGetPhysicalDeviceProperties(Device.Device, &Device.Properties);
            Device.Version = VulkanExtractVersion(Device.Properties.apiVersion);

            // Get device memory properties
            vkGetPhysicalDeviceMemoryProperties(Device.Device, &Device.MemoryProperties);

            // Get device features
            vkGetPhysicalDeviceFeatures(Device.Device, &Device.Features);

            // Get queue families
            {
                uint32_t QueueFamilyCount;
                vkGetPhysicalDeviceQueueFamilyProperties(Device.Device, &QueueFamilyCount, nullptr);
                Device.QueueFamilies.resize(QueueFamilyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(Device.Device, &QueueFamilyCount, Device.QueueFamilies.data());
            }

            // Enumerate global device extensions
            {
                uint32_t ExtensionCount;
                vkEnumerateDeviceExtensionProperties(Device.Device, nullptr, &ExtensionCount, nullptr);
                Device.Extensions.resize(ExtensionCount);
                vkEnumerateDeviceExtensionProperties(Device.Device, nullptr, &ExtensionCount, Device.Extensions.data());
            }

            // Enumerate device layers
            uint32_t DeviceLayerCount;
            vkEnumerateDeviceLayerProperties(Device.Device, &DeviceLayerCount, nullptr);
            std::vector<VkLayerProperties> DeviceLayers(DeviceLayerCount);
            vkEnumerateDeviceLayerProperties(Device.Device, &DeviceLayerCount, DeviceLayers.data());

            Device.Layers.resize(DeviceLayerCount);
            for(uint32_t LayerIndex = 0; LayerIndex < DeviceLayerCount; ++LayerIndex)
            {
                SVulkanLayer& Layer = Device.Layers[LayerIndex];
                Layer.Properties = DeviceLayers[LayerIndex];

                uint32_t LayerExtensionCount;
                vkEnumerateDeviceExtensionProperties(Device.Device, Layer.Properties.layerName, &LayerExtensionCount, nullptr);
                Layer.Extensions.resize(LayerExtensionCount);
                vkEnumerateDeviceExtensionProperties(Device.Device, Layer.Properties.layerName, &LayerExtensionCount, Layer.Extensions.data());
            }
        }
    }

    // Create surface
    {
        VkWin32SurfaceCreateInfoKHR SurfaceCreateInfo = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
        SurfaceCreateInfo.pNext = nullptr;
        SurfaceCreateInfo.flags = 0;
        SurfaceCreateInfo.hinstance = Instance;
        SurfaceCreateInfo.hwnd = Window;

        vkCreateWin32SurfaceKHR(VulkanState.Instance, &SurfaceCreateInfo, nullptr, &VulkanState.Surface);
    }

    // Select device
    for(SVulkanPhysicalDevice& Device : VulkanState.PhysicalDevices)
    {
        uint32_t QueueFamilyIndex;
        for(QueueFamilyIndex = 0; QueueFamilyIndex < Device.QueueFamilies.size(); ++QueueFamilyIndex)
        {
            const VkQueueFamilyProperties& QueueFamily = Device.QueueFamilies[QueueFamilyIndex];

            VkQueueFlagBits RequiredFlags[] =
            {
                VK_QUEUE_GRAPHICS_BIT,
                VK_QUEUE_COMPUTE_BIT,
                VK_QUEUE_TRANSFER_BIT,
            };
            uint32_t RequiredFlagCount = ArrayCount(RequiredFlags);

            uint32_t FlagIndex;
            for(FlagIndex = 0; FlagIndex < RequiredFlagCount; ++FlagIndex)
            {
                if((QueueFamily.queueFlags & RequiredFlags[FlagIndex]) == 0)
                {
                    break;
                }
            }

            if(FlagIndex < RequiredFlagCount) continue;

            VkBool32 IsSurfaceSupported;
            vkGetPhysicalDeviceSurfaceSupportKHR(Device.Device, QueueFamilyIndex, VulkanState.Surface, &IsSurfaceSupported);

            if(!IsSurfaceSupported) continue;

            uint32_t SupportedSurfaceFormatCount;
            vkGetPhysicalDeviceSurfaceFormatsKHR(Device.Device, VulkanState.Surface, &SupportedSurfaceFormatCount, nullptr);
            std::vector<VkSurfaceFormatKHR> SupportedSurfaceFormats(SupportedSurfaceFormatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(Device.Device, VulkanState.Surface, &SupportedSurfaceFormatCount, SupportedSurfaceFormats.data());

            VkFormat DesiredSurfaceFormat = VK_FORMAT_B8G8R8A8_UNORM;
            VulkanState.SurfaceFormat = VK_FORMAT_UNDEFINED;
            for(const VkSurfaceFormatKHR& CurrentSurfaceFormat : SupportedSurfaceFormats)
            {
                VulkanState.SurfaceFormat = CurrentSurfaceFormat.format;
                VulkanState.SurfaceColorSpace = CurrentSurfaceFormat.colorSpace;
                if(VulkanState.SurfaceFormat == DesiredSurfaceFormat)
                {
                    break;
                }
            }

            if(VulkanState.SurfaceFormat == VK_FORMAT_UNDEFINED)
            {
                continue;
            }
            else if(VulkanState.SurfaceFormat == DesiredSurfaceFormat)
            {
                break;
            }
            else
            {
                printf("Warning: using undesired surface format %x\n", VulkanState.SurfaceFormat);
            }
        }

        if(QueueFamilyIndex < Device.QueueFamilies.size())
        {
            VulkanState.SelectedDevice = Device.Device;
            VulkanState.SelectedDeviceQueueFamilyIndex = QueueFamilyIndex;
            break;
        }
    }

    if(VulkanState.SelectedDevice == VK_NULL_HANDLE)
    {
        printf("Couldn't find suitable device\n");
        return -1;
    }

    // Get surface properties
    {
        // Extent
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VulkanState.SelectedDevice, VulkanState.Surface, &VulkanState.SurfaceCapabilities);
        if(VulkanState.SurfaceCapabilities.currentExtent.width == UINT32_MAX)
        {
            VkExtent2D MaxExtent = VulkanState.SurfaceCapabilities.maxImageExtent;
            VkExtent2D MinExtent = VulkanState.SurfaceCapabilities.minImageExtent;

            VulkanState.SurfaceExtent.width = Clamp(Width, MinExtent.width, MaxExtent.width);
            VulkanState.SurfaceExtent.height = Clamp(Height, MinExtent.height, MaxExtent.height);
        }
        else
        {
            VulkanState.SurfaceExtent = VulkanState.SurfaceCapabilities.currentExtent;
        }

        // Present mode
        uint32_t SurfacePresentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(VulkanState.SelectedDevice, VulkanState.Surface, &SurfacePresentModeCount, nullptr);
        std::vector<VkPresentModeKHR> SurfacePresentModes(SurfacePresentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(VulkanState.SelectedDevice, VulkanState.Surface, &SurfacePresentModeCount, SurfacePresentModes.data());

        uint32_t PresentModeIndex;
        for(PresentModeIndex = 0; PresentModeIndex < SurfacePresentModes.size(); PresentModeIndex++)
        {
            const VkPresentModeKHR& PresentMode = SurfacePresentModes[PresentModeIndex];
            if(PresentMode == VK_PRESENT_MODE_FIFO_KHR)
            {
                VulkanState.SurfacePresentMode = PresentMode;
                break;
            }
        }

        if(PresentModeIndex >= SurfacePresentModes.size())
        {
            printf("Couldn't find suitable present mode\n");
            return -1;
        }
    }

    // Create logical device
    {
        float QueuePriorities[1] = { 0.0f };
        VkDeviceQueueCreateInfo QueueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        QueueCreateInfo.pNext = nullptr;
        QueueCreateInfo.flags = 0;
        QueueCreateInfo.queueFamilyIndex = VulkanState.SelectedDeviceQueueFamilyIndex;
        QueueCreateInfo.queueCount = 1;
        QueueCreateInfo.pQueuePriorities = QueuePriorities;

        const char* const EnabledDeviceExtensions[] =
        {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };

        uint32_t EnabledDeviceExtensionCount = ArrayCount(EnabledDeviceExtensions);

        // TODO(boti): check for device extension support

        VkDeviceCreateInfo DeviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        DeviceCreateInfo.pNext = nullptr;
        DeviceCreateInfo.flags = 0;
        DeviceCreateInfo.queueCreateInfoCount = 1;
        DeviceCreateInfo.pQueueCreateInfos = &QueueCreateInfo;
        DeviceCreateInfo.enabledLayerCount = 0;
        DeviceCreateInfo.ppEnabledLayerNames = nullptr;
        DeviceCreateInfo.enabledExtensionCount = EnabledDeviceExtensionCount;
        DeviceCreateInfo.ppEnabledExtensionNames = EnabledDeviceExtensions;
        DeviceCreateInfo.pEnabledFeatures = nullptr;

        vkCreateDevice(VulkanState.SelectedDevice, &DeviceCreateInfo, nullptr, &VulkanState.Device);

        // Get queue
        vkGetDeviceQueue(VulkanState.Device, VulkanState.SelectedDeviceQueueFamilyIndex, 0, &VulkanState.Queue);
    }

    // Create swapchain
    {
        // Create swapchain
        VkSwapchainCreateInfoKHR SwapchainCreateInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        SwapchainCreateInfo.pNext = nullptr;
        SwapchainCreateInfo.flags = 0;
        SwapchainCreateInfo.surface = VulkanState.Surface;
        SwapchainCreateInfo.minImageCount = VulkanState.SurfaceCapabilities.minImageCount;
        SwapchainCreateInfo.imageFormat = VulkanState.SurfaceFormat;
        SwapchainCreateInfo.imageColorSpace = VulkanState.SurfaceColorSpace;
        SwapchainCreateInfo.imageExtent = VulkanState.SurfaceExtent;
        SwapchainCreateInfo.imageArrayLayers = 1;
        SwapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        SwapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        SwapchainCreateInfo.queueFamilyIndexCount = 1;
        SwapchainCreateInfo.pQueueFamilyIndices = &VulkanState.SelectedDeviceQueueFamilyIndex;
        SwapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        SwapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        SwapchainCreateInfo.presentMode = VulkanState.SurfacePresentMode;
        SwapchainCreateInfo.clipped = VK_TRUE;
        SwapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

        vkCreateSwapchainKHR(VulkanState.Device, &SwapchainCreateInfo, nullptr, &VulkanState.Swapchain);

        // Get images
        uint32_t SwapchainImageCount;
        vkGetSwapchainImagesKHR(VulkanState.Device, VulkanState.Swapchain, &SwapchainImageCount, nullptr);
        VulkanState.SwapchainImages.resize(SwapchainImageCount);
        vkGetSwapchainImagesKHR(VulkanState.Device, VulkanState.Swapchain, &SwapchainImageCount, VulkanState.SwapchainImages.data());

        // Create image views
        VulkanState.SwapchainImageViews.resize(SwapchainImageCount);
        for(uint32_t ImageIndex = 0; ImageIndex < SwapchainImageCount; ++ImageIndex)
        {
            VkImageViewCreateInfo ImageViewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            ImageViewCreateInfo.pNext = nullptr;
            ImageViewCreateInfo.flags = 0;
            ImageViewCreateInfo.image = VulkanState.SwapchainImages[ImageIndex];
            ImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ImageViewCreateInfo.format = VulkanState.SurfaceFormat;
            ImageViewCreateInfo.components =
            {
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY
            };
            ImageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
            ImageViewCreateInfo.subresourceRange.levelCount = 1;
            ImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
            ImageViewCreateInfo.subresourceRange.layerCount = 1;

            vkCreateImageView(VulkanState.Device, &ImageViewCreateInfo, nullptr, &VulkanState.SwapchainImageViews[ImageIndex]);
        }
    }

    // Setup graphics pipeline
    {
        // Create shader modules
        {
            SBuffer ShaderBin = win32LoadFile("Shaders/shader.spv");

            VkShaderModuleCreateInfo ShaderCreateInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            ShaderCreateInfo.pNext = nullptr;
            ShaderCreateInfo.flags = 0;
            ShaderCreateInfo.codeSize = ShaderBin.Size;
            ShaderCreateInfo.pCode = (uint32_t*)ShaderBin.Data;
            vkCreateShaderModule(VulkanState.Device, &ShaderCreateInfo, nullptr, &VulkanState.Shader);
        }

        /* ================================== */
        VkPipelineShaderStageCreateInfo VertexShaderStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        VertexShaderStage.pNext = nullptr;
        VertexShaderStage.flags = 0;
        VertexShaderStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        VertexShaderStage.module = VulkanState.Shader;
        VertexShaderStage.pName = "main";
        VertexShaderStage.pSpecializationInfo = nullptr;

        VkPipelineShaderStageCreateInfo FragmentShaderStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        FragmentShaderStage.pNext = nullptr;
        FragmentShaderStage.flags = 0;
        FragmentShaderStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        FragmentShaderStage.module = VulkanState.Shader;
        FragmentShaderStage.pName = "main";
        FragmentShaderStage.pSpecializationInfo = nullptr;

        VkPipelineShaderStageCreateInfo ShaderStages[] =
        {
            VertexShaderStage,
            FragmentShaderStage,
        };
        uint32_t ShaderStageCount = ArrayCount(ShaderStages);

        /* ================================== */
        VkPipelineVertexInputStateCreateInfo VertexInputState = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        VertexInputState.pNext = nullptr;
        VertexInputState.flags = 0;
        VertexInputState.vertexBindingDescriptionCount = 0;
        VertexInputState.pVertexBindingDescriptions = nullptr;
        VertexInputState.vertexAttributeDescriptionCount = 0;
        VertexInputState.pVertexAttributeDescriptions = nullptr;

        /* ================================== */
        VkPipelineInputAssemblyStateCreateInfo InputAssemblyState = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        InputAssemblyState.pNext = nullptr;
        InputAssemblyState.flags = 0;
        InputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        InputAssemblyState.primitiveRestartEnable = VK_FALSE;

        /* ================================== */
        VkViewport Viewport = {};
        Viewport.x = 0.0f;
        Viewport.y = 0.0f;
        Viewport.width = (float)Width;
        Viewport.height = (float)Height;
        Viewport.minDepth = 0.0f;
        Viewport.maxDepth = 1.0f;

        VkRect2D Scissor = {};
        Scissor.offset = { 0, 0 };
        Scissor.extent = { Width, Height };

        VkPipelineViewportStateCreateInfo ViewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        ViewportState.pNext = nullptr;
        ViewportState.flags = 0;
        ViewportState.viewportCount = 1;
        ViewportState.pViewports = &Viewport;
        ViewportState.scissorCount = 1;
        ViewportState.pScissors = &Scissor;

        /* ================================== */
        VkPipelineRasterizationStateCreateInfo RasterizationState = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        RasterizationState.pNext = nullptr;
        RasterizationState.flags = 0;
        RasterizationState.depthClampEnable = VK_FALSE;
        RasterizationState.rasterizerDiscardEnable = VK_FALSE;
        RasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
        RasterizationState.cullMode = VK_CULL_MODE_NONE;
        RasterizationState.frontFace = VK_FRONT_FACE_CLOCKWISE;
        RasterizationState.depthBiasEnable = VK_FALSE;
        RasterizationState.depthBiasConstantFactor = 0.0f;
        RasterizationState.depthBiasClamp = 0.0f;
        RasterizationState.depthBiasSlopeFactor = 0.0f;
        RasterizationState.lineWidth = 1.0f;

        /* ================================== */
        VkPipelineMultisampleStateCreateInfo MultisampleState = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        MultisampleState.pNext = nullptr;
        MultisampleState.flags = 0;
        MultisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        MultisampleState.sampleShadingEnable = VK_FALSE;
        MultisampleState.minSampleShading = 0.0f;
        MultisampleState.pSampleMask = nullptr;
        MultisampleState.alphaToCoverageEnable = VK_FALSE;
        MultisampleState.alphaToOneEnable = VK_FALSE;

        /* ================================== */
        VkPipelineColorBlendAttachmentState ColorBlendAttachmentState = {};
        ColorBlendAttachmentState.blendEnable = VK_FALSE;
        ColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        ColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        ColorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
        ColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        ColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        ColorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
        ColorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo ColorBlendState = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        ColorBlendState.pNext = nullptr;
        ColorBlendState.flags = 0;
        ColorBlendState.logicOpEnable = VK_FALSE;
        ColorBlendState.logicOp = VK_LOGIC_OP_COPY;
        ColorBlendState.attachmentCount = 1;
        ColorBlendState.pAttachments = &ColorBlendAttachmentState;
        ColorBlendState.blendConstants[0] = 1.0f;
        ColorBlendState.blendConstants[1] = 1.0f;
        ColorBlendState.blendConstants[2] = 1.0f;
        ColorBlendState.blendConstants[3] = 1.0f;

        /* ================================== */
        VkPipelineLayoutCreateInfo PipelineLayoutCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        PipelineLayoutCreateInfo.pNext = nullptr;
        PipelineLayoutCreateInfo.flags = 0;
        PipelineLayoutCreateInfo.setLayoutCount = 0;
        PipelineLayoutCreateInfo.pSetLayouts = nullptr;
        PipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        PipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

        VkPipelineLayout PipelineLayout;
        vkCreatePipelineLayout(VulkanState.Device, &PipelineLayoutCreateInfo, nullptr, &PipelineLayout);

        /* ================================== */
        VkAttachmentDescription ColorAttachment = {};
        ColorAttachment.flags = 0;
        ColorAttachment.format = VulkanState.SurfaceFormat;
        ColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        ColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        ColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ColorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ColorAttachmentReference = {};
        ColorAttachmentReference.attachment = 0;
        ColorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription Subpass = {};
        Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        Subpass.colorAttachmentCount = 1;
        Subpass.pColorAttachments = &ColorAttachmentReference;

        VkRenderPassCreateInfo RenderPassCreateInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        RenderPassCreateInfo.pNext = nullptr;
        RenderPassCreateInfo.flags = 0;
        RenderPassCreateInfo.attachmentCount = 1;
        RenderPassCreateInfo.pAttachments = &ColorAttachment;
        RenderPassCreateInfo.subpassCount = 1;
        RenderPassCreateInfo.pSubpasses = &Subpass;
        RenderPassCreateInfo.dependencyCount = 0;
        RenderPassCreateInfo.pDependencies = nullptr;

        vkCreateRenderPass(VulkanState.Device, &RenderPassCreateInfo, nullptr, &VulkanState.RenderPass);

        /* ================================== */
        VkGraphicsPipelineCreateInfo PipelineInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        PipelineInfo.pNext = nullptr;
        PipelineInfo.flags = 0;
        PipelineInfo.stageCount = ShaderStageCount;
        PipelineInfo.pStages = ShaderStages;
        PipelineInfo.pVertexInputState = &VertexInputState;
        PipelineInfo.pInputAssemblyState = &InputAssemblyState;
        PipelineInfo.pTessellationState = nullptr;
        PipelineInfo.pViewportState = &ViewportState;
        PipelineInfo.pRasterizationState = &RasterizationState;
        PipelineInfo.pMultisampleState = &MultisampleState;
        PipelineInfo.pDepthStencilState = nullptr;
        PipelineInfo.pColorBlendState = &ColorBlendState;
        PipelineInfo.pDynamicState = nullptr;
        PipelineInfo.layout = PipelineLayout;
        PipelineInfo.renderPass = VulkanState.RenderPass;
        PipelineInfo.subpass = 0;
        PipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        PipelineInfo.basePipelineIndex = -1;

        vkCreateGraphicsPipelines(VulkanState.Device, VK_NULL_HANDLE, 1, &PipelineInfo, nullptr, &VulkanState.Pipeline);
    }

    // Create framebuffers
    {
        VulkanState.Framebuffers.resize(VulkanState.SwapchainImages.size());
        for(uint32_t ImageIndex = 0; ImageIndex < VulkanState.SwapchainImages.size(); ++ImageIndex)
        {
            VkImageView Attachments[] =
            {
                VulkanState.SwapchainImageViews[ImageIndex],
            };

            uint32_t AttachmentCount = ArrayCount(Attachments);

            VkFramebufferCreateInfo FramebufferCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            FramebufferCreateInfo.pNext = nullptr;
            FramebufferCreateInfo.flags = 0;
            FramebufferCreateInfo.renderPass = VulkanState.RenderPass;
            FramebufferCreateInfo.attachmentCount = AttachmentCount;
            FramebufferCreateInfo.pAttachments = Attachments;
            FramebufferCreateInfo.width = VulkanState.SurfaceExtent.width;
            FramebufferCreateInfo.height = VulkanState.SurfaceExtent.height;
            FramebufferCreateInfo.layers = 1;

            vkCreateFramebuffer(VulkanState.Device, &FramebufferCreateInfo, nullptr, &VulkanState.Framebuffers[ImageIndex]);
        }
    }

    // Create command pool
    {
        VkCommandPoolCreateInfo CommandPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        CommandPoolCreateInfo.pNext = nullptr;
        CommandPoolCreateInfo.queueFamilyIndex = VulkanState.SelectedDeviceQueueFamilyIndex;
        CommandPoolCreateInfo.flags = 0;

        vkCreateCommandPool(VulkanState.Device, &CommandPoolCreateInfo, nullptr, &VulkanState.CommandPool);
    }

    // Allocate command buffers
    {
        VulkanState.CommandBuffers.resize(VulkanState.SwapchainImages.size());
        VkCommandBufferAllocateInfo CommandBufferInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        CommandBufferInfo.pNext = nullptr;
        CommandBufferInfo.commandPool = VulkanState.CommandPool;
        CommandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        CommandBufferInfo.commandBufferCount = (uint32_t)VulkanState.CommandBuffers.size();

        vkAllocateCommandBuffers(VulkanState.Device, &CommandBufferInfo, VulkanState.CommandBuffers.data());
    }

    // Record command buffers
    {
        for(uint32_t BufferIndex = 0; BufferIndex < VulkanState.CommandBuffers.size(); ++BufferIndex)
        {
            VkCommandBufferBeginInfo CommandBufferBeginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            CommandBufferBeginInfo.pNext = nullptr;
            CommandBufferBeginInfo.flags = 0;
            CommandBufferBeginInfo.pInheritanceInfo;

            VkCommandBuffer& CommandBuffer = VulkanState.CommandBuffers[BufferIndex];
            vkBeginCommandBuffer(CommandBuffer, &CommandBufferBeginInfo);
            {
                VkClearValue ClearValue = { 0.0f, 0.0f, 0.0f, 0.0f };

                VkRenderPassBeginInfo RenderPassBeginInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
                RenderPassBeginInfo.pNext = nullptr;
                RenderPassBeginInfo.renderPass = VulkanState.RenderPass;
                RenderPassBeginInfo.framebuffer = VulkanState.Framebuffers[BufferIndex];
                RenderPassBeginInfo.renderArea.offset = { 0, 0 };
                RenderPassBeginInfo.renderArea.extent = VulkanState.SurfaceExtent;
                RenderPassBeginInfo.clearValueCount = 1;
                RenderPassBeginInfo.pClearValues = &ClearValue;

                vkCmdBeginRenderPass(CommandBuffer, &RenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, VulkanState.Pipeline);
                vkCmdDraw(CommandBuffer, 3, 1, 0, 0);

                vkCmdEndRenderPass(CommandBuffer);

            }
            vkEndCommandBuffer(CommandBuffer);
        }
    }

    VkSemaphore ImageAvailableSemaphore, RenderFinishedSemaphore;
    {
        VkSemaphoreCreateInfo SemaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

        vkCreateSemaphore(VulkanState.Device, &SemaphoreCreateInfo, nullptr, &ImageAvailableSemaphore);
        vkCreateSemaphore(VulkanState.Device, &SemaphoreCreateInfo, nullptr, &RenderFinishedSemaphore);
    }

    bool bRunning = true;
    while(bRunning)
    {
        MSG Message = {};
        while(PeekMessage(&Message, nullptr, 0, 0, PM_REMOVE))
        {
            if(Message.message == WM_QUIT)
            {
                bRunning = false;
                break;
            }

            TranslateMessage(&Message);
            DispatchMessage(&Message);
        }


        // Render
        {
            uint32_t ImageIndex;
            vkAcquireNextImageKHR(VulkanState.Device, VulkanState.Swapchain, UINT64_MAX, ImageAvailableSemaphore, VK_NULL_HANDLE, &ImageIndex);

            VkSemaphore WaitSemaphores[] = { ImageAvailableSemaphore };
            VkPipelineStageFlags WaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
            VkSemaphore SignalSemaphores[] = { RenderFinishedSemaphore };

            VkSubmitInfo SubmitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            SubmitInfo.pNext = nullptr;
            SubmitInfo.waitSemaphoreCount = 1;
            SubmitInfo.pWaitSemaphores = WaitSemaphores;
            SubmitInfo.pWaitDstStageMask = WaitStages;
            SubmitInfo.commandBufferCount = 1;
            SubmitInfo.pCommandBuffers = &VulkanState.CommandBuffers[ImageIndex];
            SubmitInfo.signalSemaphoreCount = 1;
            SubmitInfo.pSignalSemaphores = SignalSemaphores;
            
            vkQueueSubmit(VulkanState.Queue, 1, &SubmitInfo, VK_NULL_HANDLE);

            VkPresentInfoKHR PresentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            PresentInfo.pNext = nullptr;
            PresentInfo.waitSemaphoreCount = 1;
            PresentInfo.pWaitSemaphores = SignalSemaphores;
            PresentInfo.swapchainCount = 1;
            PresentInfo.pSwapchains = &VulkanState.Swapchain;
            PresentInfo.pImageIndices = &ImageIndex;
            PresentInfo.pResults = nullptr;
            
            vkQueuePresentKHR(VulkanState.Queue, &PresentInfo);
            vkQueueWaitIdle(VulkanState.Queue);
        }
    }

    return 0;
}