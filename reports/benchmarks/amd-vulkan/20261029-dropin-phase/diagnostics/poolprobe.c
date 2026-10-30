#define _POSIX_C_SOURCE 200809L
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <time.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
int main(void){
 VkApplicationInfo a={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_2};
 VkInstanceCreateInfo ic={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&a};VkInstance in;vkCreateInstance(&ic,0,&in);
 uint32_t n=8;VkPhysicalDevice pd[8];vkEnumeratePhysicalDevices(in,&n,pd);VkPhysicalDevice p=0;
 for(uint32_t i=0;i<n;i++){VkPhysicalDeviceProperties pr;vkGetPhysicalDeviceProperties(pd[i],&pr);if(pr.deviceType!=VK_PHYSICAL_DEVICE_TYPE_CPU){p=pd[i];break;}}
 float pri=1;VkDeviceQueueCreateInfo q={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&pri};
 VkDeviceCreateInfo dc={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&q};VkDevice dev;vkCreateDevice(p,&dc,0,&dev);
 VkDescriptorType types[5]={VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,VK_DESCRIPTOR_TYPE_SAMPLER};
 uint32_t per[]={2,64,256,512,1024}; uint32_t sets[]={1,16,64,128,256};
 for(int k=0;k<5;k++){VkDescriptorPoolSize s[5];for(int i=0;i<5;i++)s[i]=(VkDescriptorPoolSize){types[i],per[k]};
  VkDescriptorPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.flags=1,.maxSets=sets[k],.poolSizeCount=5,.pPoolSizes=s};
  double bc=1e9,bd=1e9;for(int r=0;r<50;r++){VkDescriptorPool pool;double x=now();vkCreateDescriptorPool(dev,&ci,0,&pool);double y=now();vkDestroyDescriptorPool(dev,pool,0);double z=now();if(y-x<bc)bc=y-x;if(z-y<bd)bd=z-y;}
  printf("per-type %u sets %u: create %.1fus destroy %.1fus\n",per[k],sets[k],bc,bd);}
 return 0;}
