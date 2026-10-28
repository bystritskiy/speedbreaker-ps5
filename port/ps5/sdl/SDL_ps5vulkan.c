/* SpeedBreaker PS5: a Vulkan WSI for SDL's offscreen window host, MIT.
 * The display is real: VK_KHR_display is provided by RADV's PS5 VideoOut WSI.
 * SDL's loader is seeded from the statically linked ICD, without dlopen. */
#include "SDL_internal.h"
#if defined(SDL_VIDEO_VULKAN) && defined(SDL_VIDEO_DRIVER_OFFSCREEN)
#include "../SDL_vulkan_internal.h"
#include "../SDL_sysvideo.h"
extern PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance,const char *);
bool OFFSCREEN_Vulkan_LoadLibrary(SDL_VideoDevice *video,const char *path)
{
    (void)path;
    if(video->vulkan_config.loader_handle)return SDL_SetError("PS5 Vulkan already loaded");
    PFN_vkEnumerateInstanceExtensionProperties enumerate=(PFN_vkEnumerateInstanceExtensionProperties)
        vk_icdGetInstanceProcAddr(VK_NULL_HANDLE,"vkEnumerateInstanceExtensionProperties");
    if(!enumerate)return SDL_SetError("RADV has no instance extension enumerator");
    Uint32 count=0;VkExtensionProperties *extensions=SDL_Vulkan_CreateInstanceExtensionsList(enumerate,&count);
    if(!extensions)return false;
    bool surface=false,display=false;
    for(Uint32 i=0;i<count;i++) {
        if(!SDL_strcmp(extensions[i].extensionName,VK_KHR_SURFACE_EXTENSION_NAME))surface=true;
        if(!SDL_strcmp(extensions[i].extensionName,VK_KHR_DISPLAY_EXTENSION_NAME))display=true;
    }
    SDL_free(extensions);
    if(!surface||!display)return SDL_SetError("RADV must expose VK_KHR_surface and VK_KHR_display");
    video->vulkan_config.loader_handle=(void *)(uintptr_t)1;
    video->vulkan_config.vkGetInstanceProcAddr=(SDL_FunctionPointer)vk_icdGetInstanceProcAddr;
    video->vulkan_config.vkEnumerateInstanceExtensionProperties=(SDL_FunctionPointer)enumerate;
    return true;
}
void OFFSCREEN_Vulkan_UnloadLibrary(SDL_VideoDevice *video)
{
    video->vulkan_config.loader_handle=NULL;
    video->vulkan_config.vkGetInstanceProcAddr=NULL;
    video->vulkan_config.vkEnumerateInstanceExtensionProperties=NULL;
}
const char *const *OFFSCREEN_Vulkan_GetInstanceExtensions(SDL_VideoDevice *video,Uint32 *count)
{
    (void)video;
    static const char *const names[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_DISPLAY_EXTENSION_NAME};
    if(count)*count=SDL_arraysize(names);return names;
}
#define GET(type,name) type name=(type)vk_icdGetInstanceProcAddr(instance,#name);if(!name)return SDL_SetError("RADV missing " #name)
bool OFFSCREEN_Vulkan_CreateSurface(SDL_VideoDevice *video,SDL_Window *window,VkInstance instance,
                                    const VkAllocationCallbacks *allocator,VkSurfaceKHR *surface)
{
    *surface=VK_NULL_HANDLE;
    if(!video->vulkan_config.loader_handle)return SDL_SetError("PS5 Vulkan not loaded");
    GET(PFN_vkEnumeratePhysicalDevices,vkEnumeratePhysicalDevices);
    GET(PFN_vkGetPhysicalDeviceDisplayPropertiesKHR,vkGetPhysicalDeviceDisplayPropertiesKHR);
    GET(PFN_vkGetDisplayModePropertiesKHR,vkGetDisplayModePropertiesKHR);
    GET(PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR,vkGetPhysicalDeviceDisplayPlanePropertiesKHR);
    GET(PFN_vkGetDisplayPlaneSupportedDisplaysKHR,vkGetDisplayPlaneSupportedDisplaysKHR);
    GET(PFN_vkGetDisplayPlaneCapabilitiesKHR,vkGetDisplayPlaneCapabilitiesKHR);
    GET(PFN_vkCreateDisplayPlaneSurfaceKHR,vkCreateDisplayPlaneSurfaceKHR);
    Uint32 gpu_count=0;
    if(vkEnumeratePhysicalDevices(instance,&gpu_count,NULL)!=VK_SUCCESS||!gpu_count)
        return SDL_SetError("RADV has no physical GPU");
    VkPhysicalDevice *gpus=SDL_calloc(gpu_count,sizeof(*gpus));
    if(!gpus)return false;
    VkResult rc=vkEnumeratePhysicalDevices(instance,&gpu_count,gpus);
    if(rc!=VK_SUCCESS||!gpu_count){SDL_free(gpus);return SDL_SetError("RADV GPU enumeration failed");}
    VkPhysicalDevice gpu=gpus[0];SDL_free(gpus);
    Uint32 display_count=0;
    if(vkGetPhysicalDeviceDisplayPropertiesKHR(gpu,&display_count,NULL)!=VK_SUCCESS||!display_count)
        return SDL_SetError("VideoOut has no Vulkan display");
    VkDisplayPropertiesKHR *displays=SDL_calloc(display_count,sizeof(*displays));
    if(!displays)return false;
    rc=vkGetPhysicalDeviceDisplayPropertiesKHR(gpu,&display_count,displays);
    if(rc!=VK_SUCCESS||!display_count){SDL_free(displays);return SDL_SetError("VideoOut display enumeration failed");}
    VkDisplayKHR display=displays[0].display;
    VkSurfaceTransformFlagsKHR transforms=displays[0].supportedTransforms;SDL_free(displays);
    if(rc!=VK_SUCCESS||!(transforms&VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))
        return SDL_SetError("PS5 display does not support the identity transform");
    Uint32 mode_count=0;
    if(vkGetDisplayModePropertiesKHR(gpu,display,&mode_count,NULL)!=VK_SUCCESS||!mode_count)
        return SDL_SetError("VideoOut has no display modes");
    VkDisplayModePropertiesKHR *modes=SDL_calloc(mode_count,sizeof(*modes));
    if(!modes)return false;
    rc=vkGetDisplayModePropertiesKHR(gpu,display,&mode_count,modes);
    if(rc!=VK_SUCCESS||!mode_count){SDL_free(modes);return SDL_SetError("VideoOut mode enumeration failed");}
    Uint32 best=0;Uint64 best_score=0;
    for(Uint32 i=0;i<mode_count;i++) {
        Uint32 w=modes[i].parameters.visibleRegion.width,h=modes[i].parameters.visibleRegion.height;
        Uint32 hz=modes[i].parameters.refreshRate;
        /* Prefer the requested extent at 60 Hz, then any 60 Hz VideoOut mode. */
        Uint64 score=(w==(Uint32)window->w&&h==(Uint32)window->h?UINT64_C(1)<<50:0)
            +(hz<=61000?UINT64_C(1)<<40:0)+(Uint64)w*h;
        if(score>best_score){best=i;best_score=score;}
    }
    VkDisplayModePropertiesKHR mode=modes[best];SDL_free(modes);
    if(rc!=VK_SUCCESS)return SDL_SetError("VideoOut mode enumeration failed");
    Uint32 plane_count=0;
    if(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu,&plane_count,NULL)!=VK_SUCCESS||!plane_count)
        return SDL_SetError("VideoOut has no display planes");
    VkDisplayPlanePropertiesKHR *planes=SDL_calloc(plane_count,sizeof(*planes));
    if(!planes)return false;
    rc=vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu,&plane_count,planes);
    if(rc!=VK_SUCCESS){SDL_free(planes);return SDL_SetError("VideoOut plane enumeration failed");}
    int selected=-1;
    for(Uint32 i=0;i<plane_count&&selected<0;i++) {
        Uint32 n=0;
        if(vkGetDisplayPlaneSupportedDisplaysKHR(gpu,i,&n,NULL)!=VK_SUCCESS||!n)continue;
        VkDisplayKHR *supported=SDL_calloc(n,sizeof(*supported));if(!supported)continue;
        if(vkGetDisplayPlaneSupportedDisplaysKHR(gpu,i,&n,supported)==VK_SUCCESS)
            for(Uint32 j=0;j<n;j++)if(supported[j]==display)selected=(int)i;
        SDL_free(supported);
    }
    if(selected<0){SDL_free(planes);return SDL_SetError("No plane supports the PS5 display");}
    VkDisplayPlaneCapabilitiesKHR caps;
    rc=vkGetDisplayPlaneCapabilitiesKHR(gpu,mode.displayMode,(Uint32)selected,&caps);
    if(rc!=VK_SUCCESS){SDL_free(planes);return SDL_SetError("PS5 display plane capabilities failed");}
    VkDisplayPlaneAlphaFlagBitsKHR alpha=VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    if(!(caps.supportedAlpha&alpha)) {
        if(caps.supportedAlpha&VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR)alpha=VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR;
        else {SDL_free(planes);return SDL_SetError("Unsupported PS5 display alpha mode");}
    }
    VkDisplaySurfaceCreateInfoKHR info={.sType=VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR,
        .displayMode=mode.displayMode,.planeIndex=(Uint32)selected,.planeStackIndex=planes[selected].currentStackIndex,
        .transform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,.globalAlpha=1.0f,.alphaMode=alpha,
        .imageExtent=mode.parameters.visibleRegion};
    SDL_free(planes);
    rc=vkCreateDisplayPlaneSurfaceKHR(instance,&info,allocator,surface);
    if(rc!=VK_SUCCESS)return SDL_SetError("PS5 display surface failed: %d",rc);
    SDL_Log("[PS5 WSI] VideoOut %ux%u %.3f Hz",info.imageExtent.width,info.imageExtent.height,
            mode.parameters.refreshRate/1000.0);
    return true;
}
void OFFSCREEN_Vulkan_DestroySurface(SDL_VideoDevice *video,VkInstance instance,VkSurfaceKHR surface,
                                     const VkAllocationCallbacks *allocator)
{
    if(video->vulkan_config.loader_handle)
        SDL_Vulkan_DestroySurface_Internal(video->vulkan_config.vkGetInstanceProcAddr,instance,surface,allocator);
}
#endif
