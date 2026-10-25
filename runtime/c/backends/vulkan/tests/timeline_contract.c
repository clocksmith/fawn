/* SPDX-License-Identifier: MIT */
#include "../src/internal.h"
#include <assert.h>
#include <stdio.h>
static uint64_t counter, signal_value;
static unsigned submitted;
static VkResult submit_result;
static VkResult get_counter(VkDevice device, VkSemaphore semaphore, uint64_t *out) {
    (void)device; assert(semaphore == 77); *out = counter; return 0;
}
static VkResult wait_timeline(VkDevice device, const VkSemaphoreWaitInfo *info, uint64_t timeout) {
    (void)device; (void)timeout;
    assert(info->sType == DOE_VK_SEMAPHORE_WAIT && info->semaphoreCount == 1 && info->pSemaphores[0] == 77);
    return counter >= info->pValues[0] ? 0 : 2;
}
static VkResult submit_native(VkQueue queue, uint32_t count, const VkSubmitInfo *submit, VkFence fence) {
    (void)queue; assert(count == 1 && !fence && submit->signalSemaphoreCount == 1);
    const VkTimelineSemaphoreSubmitInfo *timeline = submit->pNext;
    assert(timeline && timeline->sType == DOE_VK_TIMELINE_SUBMIT && timeline->signalSemaphoreValueCount == 1);
    assert(submit->pSignalSemaphores[0] == 77);
    signal_value = timeline->pSignalSemaphoreValues[0]; submitted++;
    return submit_result;
}
static VkResult end(VkCommandBuffer command) { (void)command; return 0; }
static void barrier(VkCommandBuffer command, VkFlags source, VkFlags target, VkFlags flags,
    uint32_t memory, const void *memories, uint32_t buffers, const void *buffer_barriers,
    uint32_t images, const void *image_barriers) {
    (void)command; (void)source; (void)target; (void)flags; (void)memory; (void)memories;
    (void)buffers; (void)buffer_barriers; (void)images; (void)image_barriers;
}
int main(void) {
    doe_vk_context context = {.timeline=77,.timeline_max_difference=4};
    context.vk.vkGetSemaphoreCounterValue = get_counter; context.vk.vkWaitSemaphores = wait_timeline;
    context.vk.vkQueueSubmit = submit_native; context.vk.vkEndCommandBuffer = end;
    context.vk.vkCmdPipelineBarrier = barrier;
    doe_vk_commands commands = {.context=&context,.state=DOE_VK_RECORDING};
    doe_vk_error error = {0};
    assert(doe_vk_commands_submit(&commands,&error) == DOE_VK_OK && submitted == 1 && signal_value == 1);
    assert(doe_vk_commands_poll(&commands,&error) == DOE_VK_PENDING);
    assert(doe_vk_commands_wait(&commands,0,&error) == DOE_VK_TIMEOUT && commands.state == DOE_VK_SUBMITTED);
    counter = 1;
    assert(doe_vk_commands_poll(&commands,&error) == DOE_VK_OK && commands.state == DOE_VK_COMPLETE);
    commands = (doe_vk_commands){.context=&context,.state=DOE_VK_RECORDING};
    submit_result = -1;
    assert(doe_vk_commands_submit(&commands,&error) == DOE_VK_OUT_OF_MEMORY && commands.state == DOE_VK_UNCERTAIN);
    assert(signal_value == 2 && context.timeline_value == 2);
    counter = 2;
    assert(doe_vk_commands_poll(&commands,&error) == DOE_VK_OUT_OF_MEMORY);
    assert(error.native_result == -1 && commands.state == DOE_VK_FAILED);
    commands = (doe_vk_commands){.context=&context,.state=DOE_VK_RECORDING};
    context.timeline_value = UINT64_MAX;
    assert(doe_vk_commands_submit(&commands,&error) == DOE_VK_INVALID_STATE && submitted == 2);
    commands = (doe_vk_commands){.context=&context,.state=DOE_VK_RECORDING};
    context.timeline_value = 6;
    assert(doe_vk_commands_submit(&commands,&error) == DOE_VK_BUSY && submitted == 2);
    puts("PASS timeline typed signal/wait/poll, timeout retains submission, uncertain first error, overflow and pending distance controls");
}
