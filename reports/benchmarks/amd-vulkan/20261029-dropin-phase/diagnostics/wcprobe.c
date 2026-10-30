#define _POSIX_C_SOURCE 200809L
#include <vulkan/vulkan.h>
#include <emmintrin.h>
#include <immintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
static void stream16(void*d,const void*s,size_t n){char*o=d;const char*i=s;for(size_t k=0;k<n;k+=16)_mm_stream_si128((__m128i*)(o+k),_mm_loadu_si128((const __m128i*)(i+k)));_mm_sfence();}
static void stream32(void*d,const void*s,size_t n){char*o=d;const char*i=s;for(size_t k=0;k<n;k+=32)_mm256_stream_si256((__m256i*)(o+k),_mm256_loadu_si256((const __m256i*)(i+k)));_mm_sfence();}
int main(void){
 VkApplicationInfo a={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_2};
 VkInstanceCreateInfo ic={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&a};VkInstance in;vkCreateInstance(&ic,0,&in);
 uint32_t n=8;VkPhysicalDevice pd[8];vkEnumeratePhysicalDevices(in,&n,pd);VkPhysicalDevice p=0;
 for(uint32_t i=0;i<n;i++){VkPhysicalDeviceProperties pr;vkGetPhysicalDeviceProperties(pd[i],&pr);if(pr.deviceType!=VK_PHYSICAL_DEVICE_TYPE_CPU){p=pd[i];break;}}
 float pri=1;VkDeviceQueueCreateInfo q={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&pri};
 VkDeviceCreateInfo dc={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&q};VkDevice dev;vkCreateDevice(p,&dc,0,&dev);
 VkPhysicalDeviceMemoryProperties mp;vkGetPhysicalDeviceMemoryProperties(p,&mp);
 size_t sizes[2]={1<<20,4<<20};char*src=aligned_alloc(64,4<<20);for(int i=0;i<4<<20;i++)src[i]=(char)i;
 for(uint32_t t=0;t<mp.memoryTypeCount;t++){uint32_t f=mp.memoryTypes[t].propertyFlags;if(!(f&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))continue;
  for(int si=0;si<2;si++){size_t sz=sizes[si];VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=sz,.memoryTypeIndex=t};VkDeviceMemory m;
   double t0=now();if(vkAllocateMemory(dev,&ai,0,&m))continue;void*ptr;vkMapMemory(dev,m,0,sz,0,&ptr);double t1=now();
   double best[4]={1e9,1e9,1e9,1e9};
   for(int r=0;r<30;r++){double x=now();memcpy(ptr,src,sz);double y=now();if(y-x<best[0])best[0]=y-x;
     x=now();stream16(ptr,src,sz);y=now();if(y-x<best[1])best[1]=y-x;
     x=now();stream32(ptr,src,sz);y=now();if(y-x<best[2])best[2]=y-x;
     x=now();memset(ptr,0,sz);y=now();if(y-x<best[3])best[3]=y-x;}
   double med=0;{double v[30];for(int r=0;r<30;r++){double x=now();memcpy(ptr,src,sz);v[r]=now()-x;}for(int i=0;i<30;i++)for(int j=i+1;j<30;j++)if(v[j]<v[i]){double z=v[i];v[i]=v[j];v[j]=z;}med=v[15];}
   printf("type%u flags=0x%x %zuMiB alloc+map=%.0fus memcpy=%.1f(med %.1f) stream16=%.1f stream32=%.1f memset=%.1f\n",t,f,sz>>20,t1-t0,best[0],med,best[1],best[2],best[3]);
   vkUnmapMemory(dev,m);vkFreeMemory(dev,m,0);}}
 return 0;}
