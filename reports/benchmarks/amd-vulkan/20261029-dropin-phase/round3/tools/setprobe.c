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
 VkDescriptorSetLayoutBinding b[3]={{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,0},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,0},{2,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,0}};
 VkDescriptorSetLayoutCreateInfo lc={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=b};VkDescriptorSetLayout l;vkCreateDescriptorSetLayout(dev,&lc,0,&l);
 VkDescriptorType types[5]={VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,VK_DESCRIPTOR_TYPE_SAMPLER};
 struct {const char*name;uint32_t flags,sets,per;} cfg[]={{"exact",0,1,0},{"free-1024",1,256,1024},{"free-64",1,32,64}};
 for(int c=0;c<3;c++){double tc=0,ta=0,tf=0,td=0;int R=200;
  for(int r=0;r<R;r++){VkDescriptorPoolSize s[5];uint32_t k;
   if(cfg[c].per==0){s[0]=(VkDescriptorPoolSize){types[0],1};s[1]=(VkDescriptorPoolSize){types[1],2};k=2;} else {for(int i=0;i<5;i++)s[i]=(VkDescriptorPoolSize){types[i],cfg[c].per};k=5;}
   VkDescriptorPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.flags=cfg[c].flags,.maxSets=cfg[c].sets,.poolSizeCount=k,.pPoolSizes=s};
   VkDescriptorPool pool;double x=now();vkCreateDescriptorPool(dev,&ci,0,&pool);double y=now();
   VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&l};VkDescriptorSet set;vkAllocateDescriptorSets(dev,&ai,&set);double z=now();
   if(cfg[c].flags)vkFreeDescriptorSets(dev,pool,1,&set);double w=now();vkDestroyDescriptorPool(dev,pool,0);double v=now();
   tc+=y-x;ta+=z-y;tf+=w-z;td+=v-w;}
  printf("%s: create %.1f alloc %.1f free %.1f destroy %.1f us (mean of %d)\n",cfg[c].name,tc/R,ta/R,tf/R,td/R,R);}
 return 0;}
