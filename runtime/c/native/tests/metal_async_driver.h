/* SPDX-License-Identifier: MIT
 * Existing composition ledger completion transport at the new async boundary. */
#ifndef DOE_METAL_ASYNC_DRIVER_TEST_H
#define DOE_METAL_ASYNC_DRIVER_TEST_H
static void mock_block_completion(mock_native *mock, void *command, uint64_t timeout)
{
    (void)mock; (void)timeout;
    size_t status = mock_command_status(command);
    if (!atomic_load(&hold) && (status == 2 || status == 3)) mock_command_result(command,4,0);
}
static void mock_commit(void *userdata, void *command)
{
    unsigned index = (unsigned)((mock_native *)userdata - mocks);
    assert(index < 8);
    external_drivers[index].commit(userdata,command);
    /* Existing construction ledgers model immediate completion or explicit
     * UNKNOWN. Delayed acceptance is exercised by metal_async_submit.c. */
    mock_command_result(command,atomic_load(&hold) ? 0 : 4,0);
}

#endif
