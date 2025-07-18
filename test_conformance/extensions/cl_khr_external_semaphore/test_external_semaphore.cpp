#include "harness/typeWrappers.h"
#include "harness/extensionHelpers.h"
#include "harness/errorHelpers.h"
#include "opencl_vulkan_wrapper.hpp"
#include <thread>
#include <chrono>
#include <algorithm>
#include <cinttypes>

#define SEMAPHORE_PARAM_TEST(param_name, param_type, expected)                 \
    do                                                                         \
    {                                                                          \
        param_type value;                                                      \
        size_t size;                                                           \
        cl_int error = clGetSemaphoreInfoKHR(sema, param_name, sizeof(value),  \
                                             &value, &size);                   \
        test_error(error, "Unable to get " #param_name " from semaphore");     \
        if (value != expected)                                                 \
        {                                                                      \
            test_fail("ERROR: Parameter %s did not validate! "                 \
                      "(expected %" PRIuPTR " got %" PRIuPTR ")\n",            \
                      #param_name, (uintptr_t)expected, (uintptr_t)value);     \
        }                                                                      \
        if (size != sizeof(value))                                             \
        {                                                                      \
            test_fail(                                                         \
                "ERROR: Returned size of parameter %s does not validate! "     \
                "(expected %d, got %d)\n",                                     \
                #param_name, (int)sizeof(value), (int)size);                   \
        }                                                                      \
    } while (false)

#define SEMAPHORE_PARAM_TEST_ARRAY(param_name, param_type, num_params,         \
                                   expected)                                   \
    do                                                                         \
    {                                                                          \
        param_type value[num_params];                                          \
        size_t size;                                                           \
        cl_int error = clGetSemaphoreInfoKHR(sema, param_name, sizeof(value),  \
                                             &value, &size);                   \
        test_error(error, "Unable to get " #param_name " from semaphore");     \
        if (size != sizeof(value))                                             \
        {                                                                      \
            test_fail(                                                         \
                "ERROR: Returned size of parameter %s does not validate! "     \
                "(expected %d, got %d)\n",                                     \
                #param_name, (int)sizeof(value), (int)size);                   \
        }                                                                      \
        if (memcmp(value, expected, size) != 0)                                \
        {                                                                      \
            test_fail("ERROR: Parameter %s did not validate!\n", #param_name); \
        }                                                                      \
    } while (false)

static const char *source = "__kernel void empty() {}";

static void log_info_semaphore_type(
    VulkanExternalSemaphoreHandleType vkExternalSemaphoreHandleType)
{
    std::stringstream semaphore_type_description;
    semaphore_type_description << "Testing semaphore type \""
                               << vkExternalSemaphoreHandleType << "\""
                               << std::endl;
    log_info("%s", semaphore_type_description.str().c_str());
}

static int init_vulkan_device(cl_uint num_devices, cl_device_id *deviceIds)
{
    cl_platform_id platform = nullptr;

    cl_int err = CL_SUCCESS;

    err = clGetPlatformIDs(1, &platform, NULL);
    if (err != CL_SUCCESS)
    {
        print_error(err, "Error: Failed to get platform\n");
        return err;
    }

    init_cl_vk_ext(platform, num_devices, deviceIds);

    return CL_SUCCESS;
}

static cl_int get_device_semaphore_handle_types(
    cl_device_id deviceID, cl_device_info param,
    std::vector<cl_external_semaphore_handle_type_khr> &handle_types)
{
    int err = CL_SUCCESS;
    // Query for export support
    size_t size_handle_types = 0;
    size_t num_handle_types = 0;
    err = clGetDeviceInfo(deviceID, param, 0, nullptr, &size_handle_types);
    test_error(err, "Failed to get number of exportable handle types");

    num_handle_types =
        size_handle_types / sizeof(cl_external_semaphore_handle_type_khr);

    // Empty list (size_handle_types:0) is a valid value denoting that
    // implementation is incapable to import/export semaphore
    if (num_handle_types > 0)
    {
        std::vector<cl_external_semaphore_handle_type_khr>
            handle_types_query_result(num_handle_types);
        err =
            clGetDeviceInfo(deviceID, param,
                            handle_types_query_result.size()
                                * sizeof(cl_external_semaphore_handle_type_khr),
                            handle_types_query_result.data(), nullptr);
        test_error(err, "Failed to get exportable handle types");

        for (auto handle_type : handle_types_query_result)
        {
            handle_types.push_back(handle_type);
        }
    }

    return CL_SUCCESS;
}

// Confirm the semaphores can be successfully queried
REGISTER_TEST_VERSION(external_semaphores_queries, Version(1, 2))
{
    REQUIRE_EXTENSION("cl_khr_semaphore");
    REQUIRE_EXTENSION("cl_khr_external_semaphore");

    if (init_vulkan_device(1, &device))
    {
        log_info("Cannot initialise Vulkan. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    VulkanDevice vkDevice;

    GET_PFN(device, clGetSemaphoreInfoKHR);
    GET_PFN(device, clReleaseSemaphoreKHR);
    GET_PFN(device, clRetainSemaphoreKHR);

    std::vector<VulkanExternalSemaphoreHandleType>
        vkExternalSemaphoreHandleTypeList =
            getSupportedInteropExternalSemaphoreHandleTypes(device, vkDevice);

    if (vkExternalSemaphoreHandleTypeList.empty())
    {
        test_fail("No external semaphore handle types found\n");
    }

    for (VulkanExternalSemaphoreHandleType vkExternalSemaphoreHandleType :
         vkExternalSemaphoreHandleTypeList)
    {
        log_info_semaphore_type(vkExternalSemaphoreHandleType);
        VulkanSemaphore vkVk2CLSemaphore(vkDevice,
                                         vkExternalSemaphoreHandleType);

        clExternalImportableSemaphore sema_ext(
            vkVk2CLSemaphore, context, vkExternalSemaphoreHandleType, device);

        // Needed by the macro
        cl_semaphore_khr sema = sema_ext.getCLSemaphore();

        SEMAPHORE_PARAM_TEST(CL_SEMAPHORE_TYPE_KHR, cl_semaphore_type_khr,
                             CL_SEMAPHORE_TYPE_BINARY_KHR);

        SEMAPHORE_PARAM_TEST(CL_SEMAPHORE_DEVICE_HANDLE_LIST_KHR, cl_device_id,
                             device);

        // Confirm that querying CL_SEMAPHORE_CONTEXT_KHR returns the right
        // context
        SEMAPHORE_PARAM_TEST(CL_SEMAPHORE_CONTEXT_KHR, cl_context, context);

        // Confirm that querying CL_SEMAPHORE_REFERENCE_COUNT_KHR returns the
        // right value
        SEMAPHORE_PARAM_TEST(CL_SEMAPHORE_REFERENCE_COUNT_KHR, cl_uint, 1);

        cl_int err = CL_SUCCESS;

        err = clRetainSemaphoreKHR(sema);
        test_error(err, "Could not retain semaphore");
        SEMAPHORE_PARAM_TEST(CL_SEMAPHORE_REFERENCE_COUNT_KHR, cl_uint, 2);

        err = clReleaseSemaphoreKHR(sema);
        test_error(err, "Could not release semaphore");
        SEMAPHORE_PARAM_TEST(CL_SEMAPHORE_REFERENCE_COUNT_KHR, cl_uint, 1);

        // Confirm that querying CL_SEMAPHORE_PAYLOAD_KHR returns the correct
        // state
        cl_semaphore_payload_khr expected_payload_value =
            (vkExternalSemaphoreHandleType
             == VULKAN_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD)
            ? 1
            : 0;
        SEMAPHORE_PARAM_TEST(CL_SEMAPHORE_PAYLOAD_KHR, cl_semaphore_payload_khr,
                             expected_payload_value);
    }

    return TEST_PASS;
}

REGISTER_TEST_VERSION(external_semaphores_cross_context, Version(1, 2))
{
    REQUIRE_EXTENSION("cl_khr_external_semaphore");

    GET_PFN(device, clEnqueueSignalSemaphoresKHR);
    GET_PFN(device, clEnqueueWaitSemaphoresKHR);
    GET_PFN(device, clCreateSemaphoreWithPropertiesKHR);
    GET_PFN(device, clGetSemaphoreHandleForTypeKHR);
    GET_PFN(device, clReleaseSemaphoreKHR);

    std::vector<cl_external_semaphore_handle_type_khr> import_handle_types;
    std::vector<cl_external_semaphore_handle_type_khr> export_handle_types;

    cl_int err = CL_SUCCESS;
    err = get_device_semaphore_handle_types(
        device, CL_DEVICE_SEMAPHORE_IMPORT_HANDLE_TYPES_KHR,
        import_handle_types);
    test_error(err, "Failed to query import handle types");

    err = get_device_semaphore_handle_types(
        device, CL_DEVICE_SEMAPHORE_EXPORT_HANDLE_TYPES_KHR,
        export_handle_types);
    test_error(err, "Failed to query export handle types");

    // If cl_khr_external_semaphore is reported, implementation must
    // support any of import, export or maybe both.
    if (import_handle_types.empty() && export_handle_types.empty())
    {
        test_fail("No support for import/export semaphore.\n");
    }

    // Find handles that support both import and export
    std::vector<cl_external_semaphore_handle_type_khr>
        import_export_handle_types;

    std::set_intersection(
        import_handle_types.begin(), import_handle_types.end(),
        export_handle_types.begin(), export_handle_types.end(),
        std::back_inserter(import_export_handle_types));

    cl_context context2 =
        clCreateContext(NULL, 1, &device, notify_callback, NULL, &err);
    test_error(err, "Failed to create context2");

    clCommandQueueWrapper queue1 =
        clCreateCommandQueue(context, device, 0, &err);
    test_error(err, "Could not create command queue");

    clCommandQueueWrapper queue2 =
        clCreateCommandQueue(context2, device, 0, &err);
    test_error(err, "Could not create command queue");

    if (import_export_handle_types.empty())
    {
        log_info("Could not find a handle type that supports both import and "
                 "export.\n");
        return TEST_SKIPPED_ITSELF;
    }

    for (auto handle_type : import_export_handle_types)
    {
        cl_semaphore_properties_khr export_props[] = {
            (cl_semaphore_properties_khr)CL_SEMAPHORE_TYPE_KHR,
            (cl_semaphore_properties_khr)CL_SEMAPHORE_TYPE_BINARY_KHR,
            (cl_semaphore_properties_khr)CL_SEMAPHORE_EXPORT_HANDLE_TYPES_KHR,
            (cl_semaphore_properties_khr)handle_type,
            (cl_semaphore_properties_khr)
                CL_SEMAPHORE_EXPORT_HANDLE_TYPES_LIST_END_KHR,
            (cl_semaphore_properties_khr)0
        };

        // Signal semaphore on context1
        cl_semaphore_khr exportable_semaphore =
            clCreateSemaphoreWithPropertiesKHR(context, export_props, &err);
        test_error(err, "Failed to create exportable semaphore");

        err = clEnqueueSignalSemaphoresKHR(queue1, 1, &exportable_semaphore,
                                           nullptr, 0, nullptr, nullptr);
        test_error(err, "Failed to signal semaphore on context1");

        cl_semaphore_properties_khr handle =
            0; // The handle must fit in cl_semaphore_properties_khr
        err = clGetSemaphoreHandleForTypeKHR(exportable_semaphore, device,
                                             handle_type, sizeof(handle),
                                             &handle, nullptr);
        test_error(err, "Failed to export handle from semaphore");

        // Import semaphore into context2
        cl_semaphore_properties_khr import_props[] = {
            (cl_semaphore_properties_khr)CL_SEMAPHORE_TYPE_KHR,
            (cl_semaphore_properties_khr)CL_SEMAPHORE_TYPE_BINARY_KHR,
            (cl_semaphore_properties_khr)handle_type,
            (cl_semaphore_properties_khr)handle, (cl_semaphore_properties_khr)0
        };

        cl_semaphore_khr imported_semaphore =
            clCreateSemaphoreWithPropertiesKHR(context2, import_props, &err);
        test_error(err, "Failed to import semaphore into context2 semaphore");

        err = clEnqueueWaitSemaphoresKHR(queue2, 1, &imported_semaphore,
                                         nullptr, 0, nullptr, nullptr);
        test_error(err, "Failed to signal semaphore on context1");

        err = clFlush(queue1);
        test_error(err, "Failed to flush queue1");

        err = clFinish(queue2);
        test_error(err, "Failed to finish queue2");

        err = clReleaseSemaphoreKHR(exportable_semaphore);
        test_error(err, "Failed to release semaphore");

        err = clReleaseSemaphoreKHR(imported_semaphore);
        test_error(err, "Failed to release semaphore");
    }

    err = clReleaseContext(context2);
    test_error(err, "Failed to release context2");

    return TEST_PASS;
}

// Confirm that a signal followed by a wait will complete successfully
REGISTER_TEST_VERSION(external_semaphores_simple_1, Version(1, 2))
{
    REQUIRE_EXTENSION("cl_khr_external_semaphore");

    if (init_vulkan_device(1, &device))
    {
        log_info("Cannot initialise Vulkan. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    VulkanDevice vkDevice;

    // Obtain pointers to semaphore's API
    GET_PFN(device, clEnqueueSignalSemaphoresKHR);
    GET_PFN(device, clEnqueueWaitSemaphoresKHR);

    std::vector<VulkanExternalSemaphoreHandleType>
        vkExternalSemaphoreHandleTypeList =
            getSupportedInteropExternalSemaphoreHandleTypes(device, vkDevice);

    if (vkExternalSemaphoreHandleTypeList.empty())
    {
        test_fail("No external semaphore handle types found\n");
    }

    for (VulkanExternalSemaphoreHandleType vkExternalSemaphoreHandleType :
         vkExternalSemaphoreHandleTypeList)
    {
        log_info_semaphore_type(vkExternalSemaphoreHandleType);

        VulkanSemaphore vkVk2CLSemaphore(vkDevice,
                                         vkExternalSemaphoreHandleType);

        auto sema_ext = clExternalImportableSemaphore(
            vkVk2CLSemaphore, context, vkExternalSemaphoreHandleType, device);

        cl_int err = CL_SUCCESS;

        // Create ooo queue
        clCommandQueueWrapper queue = clCreateCommandQueue(
            context, device, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
        test_error(err, "Could not create command queue");

        // Signal semaphore
        clEventWrapper signal_event;
        err = clEnqueueSignalSemaphoresKHR(queue, 1, &sema_ext.getCLSemaphore(),
                                           nullptr, 0, nullptr, &signal_event);
        test_error(err, "Could not signal semaphore");

        // Wait semaphore
        clEventWrapper wait_event;
        err = clEnqueueWaitSemaphoresKHR(queue, 1, &sema_ext.getCLSemaphore(),
                                         nullptr, 0, nullptr, &wait_event);
        test_error(err, "Could not wait semaphore");

        // Finish
        err = clFinish(queue);
        test_error(err, "Could not finish queue");

        // Ensure all events are completed
        test_assert_event_complete(signal_event);
        test_assert_event_complete(wait_event);
    }

    return TEST_PASS;
}

// Confirm that signal a semaphore with no event dependencies will not result
// in an implicit dependency on everything previously submitted
REGISTER_TEST_VERSION(external_semaphores_simple_2, Version(1, 2))
{
    return TEST_SKIPPED_ITSELF;
    REQUIRE_EXTENSION("cl_khr_external_semaphore");

    if (init_vulkan_device(1, &device))
    {
        log_info("Cannot initialise Vulkan. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    VulkanDevice vkDevice;

    // Obtain pointers to semaphore's API
    GET_PFN(device, clEnqueueSignalSemaphoresKHR);
    GET_PFN(device, clEnqueueWaitSemaphoresKHR);

    std::vector<VulkanExternalSemaphoreHandleType>
        vkExternalSemaphoreHandleTypeList =
            getSupportedInteropExternalSemaphoreHandleTypes(device, vkDevice);

    if (vkExternalSemaphoreHandleTypeList.empty())
    {
        test_fail("No external semaphore handle types found\n");
    }

    for (VulkanExternalSemaphoreHandleType vkExternalSemaphoreHandleType :
         vkExternalSemaphoreHandleTypeList)
    {
        log_info_semaphore_type(vkExternalSemaphoreHandleType);
        VulkanSemaphore vkVk2CLSemaphore(vkDevice,
                                         vkExternalSemaphoreHandleType);

        auto sema_ext = clExternalImportableSemaphore(
            vkVk2CLSemaphore, context, vkExternalSemaphoreHandleType, device);

        cl_int err = CL_SUCCESS;

        // Create ooo queue
        clCommandQueueWrapper queue = clCreateCommandQueue(
            context, device, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
        test_error(err, "Could not create command queue");

        // Create user event
        clEventWrapper user_event = clCreateUserEvent(context, &err);
        test_error(err, "Could not create user event");

        // Create Kernel
        clProgramWrapper program;
        clKernelWrapper kernel;
        err = create_single_kernel_helper(context, &program, &kernel, 1,
                                          &source, "empty");
        test_error(err, "Could not create kernel");

        // Enqueue task_1 (dependency on user_event)
        clEventWrapper task_1_event;
        err = clEnqueueTask(queue, kernel, 1, &user_event, &task_1_event);
        test_error(err, "Could not enqueue task 1");

        // Signal semaphore
        clEventWrapper signal_event;
        err = clEnqueueSignalSemaphoresKHR(queue, 1, &sema_ext.getCLSemaphore(),
                                           nullptr, 0, nullptr, &signal_event);
        test_error(err, "Could not signal semaphore");

        // Wait semaphore
        clEventWrapper wait_event;
        err = clEnqueueWaitSemaphoresKHR(queue, 1, &sema_ext.getCLSemaphore(),
                                         nullptr, 0, nullptr, &wait_event);
        test_error(err, "Could not wait semaphore");

        // Flush and delay
        err = clFlush(queue);
        test_error(err, "Could not flush queue");

        cl_event event_list[] = { signal_event, wait_event };
        err = clWaitForEvents(2, event_list);
        test_error(err, "Could not wait on events");

        // Ensure all events are completed except for task_1
        test_assert_event_inprogress(task_1_event);
        test_assert_event_complete(signal_event);
        test_assert_event_complete(wait_event);

        // Complete user_event
        err = clSetUserEventStatus(user_event, CL_COMPLETE);
        test_error(err, "Could not set user event to CL_COMPLETE");

        // Finish
        err = clFinish(queue);
        test_error(err, "Could not finish queue");

        // Ensure all events are completed
        test_assert_event_complete(task_1_event);
        test_assert_event_complete(signal_event);
        test_assert_event_complete(wait_event);
    }

    return TEST_PASS;
}

// Confirm that a semaphore can be reused multiple times
REGISTER_TEST_VERSION(external_semaphores_reuse, Version(1, 2))
{
    REQUIRE_EXTENSION("cl_khr_external_semaphore");

    if (init_vulkan_device(1, &device))
    {
        log_info("Cannot initialise Vulkan. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    VulkanDevice vkDevice;

    // Obtain pointers to semaphore's API
    GET_PFN(device, clEnqueueSignalSemaphoresKHR);
    GET_PFN(device, clEnqueueWaitSemaphoresKHR);

    std::vector<VulkanExternalSemaphoreHandleType>
        vkExternalSemaphoreHandleTypeList =
            getSupportedInteropExternalSemaphoreHandleTypes(device, vkDevice);

    if (vkExternalSemaphoreHandleTypeList.empty())
    {
        test_fail("No external semaphore handle types found\n");
    }

    for (VulkanExternalSemaphoreHandleType vkExternalSemaphoreHandleType :
         vkExternalSemaphoreHandleTypeList)
    {
        log_info_semaphore_type(vkExternalSemaphoreHandleType);
        VulkanSemaphore vkVk2CLSemaphore(vkDevice,
                                         vkExternalSemaphoreHandleType);

        auto sema_ext = clExternalImportableSemaphore(
            vkVk2CLSemaphore, context, vkExternalSemaphoreHandleType, device);

        cl_int err = CL_SUCCESS;

        // Create ooo queue
        clCommandQueueWrapper queue = clCreateCommandQueue(
            context, device, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
        test_error(err, "Could not create command queue");

        // Create Kernel
        clProgramWrapper program;
        clKernelWrapper kernel;
        err = create_single_kernel_helper(context, &program, &kernel, 1,
                                          &source, "empty");
        test_error(err, "Could not create kernel");

        constexpr size_t loop_count = 10;
        clEventWrapper signal_events[loop_count];
        clEventWrapper wait_events[loop_count];
        clEventWrapper task_events[loop_count];

        // Enqueue task_1
        err = clEnqueueTask(queue, kernel, 0, nullptr, &task_events[0]);
        test_error(err, "Unable to enqueue task_1");

        // Signal semaphore (dependency on task_1)
        err = clEnqueueSignalSemaphoresKHR(queue, 1, &sema_ext.getCLSemaphore(),
                                           nullptr, 1, &task_events[0],
                                           &signal_events[0]);
        test_error(err, "Could not signal semaphore");

        // In a loop
        size_t loop;
        for (loop = 1; loop < loop_count; ++loop)
        {
            // Wait semaphore
            err = clEnqueueWaitSemaphoresKHR(
                queue, 1, &sema_ext.getCLSemaphore(), nullptr, 0, nullptr,
                &wait_events[loop - 1]);
            test_error(err, "Could not wait semaphore");

            // Enqueue task_loop (dependency on wait)
            err = clEnqueueTask(queue, kernel, 1, &wait_events[loop - 1],
                                &task_events[loop]);
            test_error(err, "Unable to enqueue task_loop");

            // Wait for the "wait semaphore" to complete
            err = clWaitForEvents(1, &wait_events[loop - 1]);
            test_error(err, "Unable to wait for wait semaphore to complete");

            // Signal semaphore (dependency on task_loop)
            err = clEnqueueSignalSemaphoresKHR(
                queue, 1, &sema_ext.getCLSemaphore(), nullptr, 1,
                &task_events[loop], &signal_events[loop]);
            test_error(err, "Could not signal semaphore");
        }

        // Wait semaphore
        err = clEnqueueWaitSemaphoresKHR(queue, 1, &sema_ext.getCLSemaphore(),
                                         nullptr, 0, nullptr,
                                         &wait_events[loop - 1]);
        test_error(err, "Could not wait semaphore");

        // Finish
        err = clFinish(queue);
        test_error(err, "Could not finish queue");

        // Ensure all events are completed
        for (loop = 0; loop < loop_count; ++loop)
        {
            test_assert_event_complete(wait_events[loop]);
            test_assert_event_complete(signal_events[loop]);
            test_assert_event_complete(task_events[loop]);
        }
    }

    return TEST_PASS;
}

// Helper function that signals and waits on semaphore across two different
// queues.
static int external_semaphore_cross_queue_helper(cl_device_id device,
                                                 cl_context context,
                                                 cl_command_queue queue_1,
                                                 cl_command_queue queue_2)
{
    REQUIRE_EXTENSION("cl_khr_external_semaphore");

    if (init_vulkan_device(1, &device))
    {
        log_info("Cannot initialise Vulkan. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    VulkanDevice vkDevice;

    // Obtain pointers to semaphore's API
    GET_PFN(device, clEnqueueSignalSemaphoresKHR);
    GET_PFN(device, clEnqueueWaitSemaphoresKHR);

    std::vector<VulkanExternalSemaphoreHandleType>
        vkExternalSemaphoreHandleTypeList =
            getSupportedInteropExternalSemaphoreHandleTypes(device, vkDevice);

    if (vkExternalSemaphoreHandleTypeList.empty())
    {
        test_fail("No external semaphore handle types found\n");
    }

    for (VulkanExternalSemaphoreHandleType vkExternalSemaphoreHandleType :
         vkExternalSemaphoreHandleTypeList)
    {
        log_info_semaphore_type(vkExternalSemaphoreHandleType);
        VulkanSemaphore vkVk2CLSemaphore(vkDevice,
                                         vkExternalSemaphoreHandleType);

        auto sema_ext = clExternalImportableSemaphore(
            vkVk2CLSemaphore, context, vkExternalSemaphoreHandleType, device);

        cl_int err = CL_SUCCESS;

        // Signal semaphore on queue_1
        clEventWrapper signal_event;
        err =
            clEnqueueSignalSemaphoresKHR(queue_1, 1, &sema_ext.getCLSemaphore(),
                                         nullptr, 0, nullptr, &signal_event);
        test_error(err, "Could not signal semaphore");

        // Wait semaphore on queue_2
        clEventWrapper wait_event;
        err = clEnqueueWaitSemaphoresKHR(queue_2, 1, &sema_ext.getCLSemaphore(),
                                         nullptr, 0, nullptr, &wait_event);
        test_error(err, "Could not wait semaphore");

        // Finish queue_1 and queue_2
        err = clFinish(queue_1);
        test_error(err, "Could not finish queue");

        err = clFinish(queue_2);
        test_error(err, "Could not finish queue");

        // Ensure all events are completed
        test_assert_event_complete(signal_event);
        test_assert_event_complete(wait_event);
    }

    return TEST_PASS;
}


// Confirm that a semaphore works across different ooo queues
REGISTER_TEST_VERSION(external_semaphores_cross_queues_ooo, Version(1, 2))
{
    cl_int err;

    // Create ooo queues
    clCommandQueueWrapper queue_1 = clCreateCommandQueue(
        context, device, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
    test_error(err, "Could not create command queue");

    clCommandQueueWrapper queue_2 = clCreateCommandQueue(
        context, device, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
    test_error(err, "Could not create command queue");

    return external_semaphore_cross_queue_helper(device, context, queue_1,
                                                 queue_2);
}

// Confirm that a semaphore works across different in-order queues
REGISTER_TEST_VERSION(external_semaphores_cross_queues_io, Version(1, 2))
{
    cl_int err;

    // Create in-order queues
    clCommandQueueWrapper queue_1 =
        clCreateCommandQueue(context, device, 0, &err);
    test_error(err, "Could not create command queue");

    clCommandQueueWrapper queue_2 =
        clCreateCommandQueue(context, device, 0, &err);
    test_error(err, "Could not create command queue");

    return external_semaphore_cross_queue_helper(device, context, queue_1,
                                                 queue_2);
}

REGISTER_TEST_VERSION(external_semaphores_cross_queues_io2, Version(1, 2))
{
    REQUIRE_EXTENSION("cl_khr_external_semaphore");

    if (init_vulkan_device(1, &device))
    {
        log_info("Cannot initialise Vulkan. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    VulkanDevice vkDevice;

    cl_int err = CL_SUCCESS;

    clContextWrapper context2 =
        clCreateContext(NULL, 1, &device, notify_callback, NULL, &err);
    if (!context2)
    {
        print_error(err, "Unable to create testing context");
        return TEST_FAIL;
    }

    GET_PFN(device, clEnqueueSignalSemaphoresKHR);
    GET_PFN(device, clEnqueueWaitSemaphoresKHR);

    std::vector<VulkanExternalSemaphoreHandleType>
        vkExternalSemaphoreHandleTypeList =
            getSupportedInteropExternalSemaphoreHandleTypes(device, vkDevice);

    if (vkExternalSemaphoreHandleTypeList.empty())
    {
        test_fail("No external semaphore handle types found\n");
    }

    for (VulkanExternalSemaphoreHandleType vkExternalSemaphoreHandleType :
         vkExternalSemaphoreHandleTypeList)
    {
        log_info_semaphore_type(vkExternalSemaphoreHandleType);
        VulkanSemaphore vkVk2CLSemaphore(vkDevice,
                                         vkExternalSemaphoreHandleType);

        auto sema_ext_1 = clExternalImportableSemaphore(
            vkVk2CLSemaphore, context, vkExternalSemaphoreHandleType, device);

        auto sema_ext_2 = clExternalImportableSemaphore(
            vkVk2CLSemaphore, context2, vkExternalSemaphoreHandleType, device);

        clCommandQueueWrapper queue1 =
            clCreateCommandQueue(context, device, 0, &err);
        test_error(err, "Could not create command queue");

        clCommandQueueWrapper queue2 =
            clCreateCommandQueue(context2, device, 0, &err);
        test_error(err, "Could not create command queue");

        // Signal semaphore 1
        clEventWrapper signal_1_event;
        err = clEnqueueSignalSemaphoresKHR(
            queue1, 1, &sema_ext_1.getCLSemaphore(), nullptr, 0, nullptr,
            &signal_1_event);
        test_error(err, "Could not signal semaphore");

        // Wait semaphore 1
        clEventWrapper wait_1_event;
        err =
            clEnqueueWaitSemaphoresKHR(queue1, 1, &sema_ext_1.getCLSemaphore(),
                                       nullptr, 0, nullptr, &wait_1_event);
        test_error(err, "Could not wait semaphore");

        // Signal semaphore 2
        clEventWrapper signal_2_event;
        err = clEnqueueSignalSemaphoresKHR(
            queue2, 1, &sema_ext_2.getCLSemaphore(), nullptr, 0, nullptr,
            &signal_2_event);
        test_error(err, "Could not signal semaphore");

        // Wait semaphore 2
        clEventWrapper wait_2_event;
        err =
            clEnqueueWaitSemaphoresKHR(queue2, 1, &sema_ext_2.getCLSemaphore(),
                                       nullptr, 0, nullptr, &wait_2_event);
        test_error(err, "Could not wait semaphore");

        // Finish
        err = clFinish(queue1);
        test_error(err, "Could not finish queue");

        err = clFinish(queue2);
        test_error(err, "Could not finish queue");

        // Ensure all events are completed
        test_assert_event_complete(signal_1_event);
        test_assert_event_complete(signal_2_event);
        test_assert_event_complete(wait_1_event);
        test_assert_event_complete(wait_2_event);
    }

    return TEST_PASS;
}

// Confirm that we can signal multiple semaphores with one command
REGISTER_TEST_VERSION(external_semaphores_multi_signal, Version(1, 2))
{
    REQUIRE_EXTENSION("cl_khr_external_semaphore");

    if (init_vulkan_device(1, &device))
    {
        log_info("Cannot initialise Vulkan. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    VulkanDevice vkDevice;

    // Obtain pointers to semaphore's API
    GET_PFN(device, clEnqueueSignalSemaphoresKHR);
    GET_PFN(device, clEnqueueWaitSemaphoresKHR);

    std::vector<VulkanExternalSemaphoreHandleType>
        vkExternalSemaphoreHandleTypeList =
            getSupportedInteropExternalSemaphoreHandleTypes(device, vkDevice);

    if (vkExternalSemaphoreHandleTypeList.empty())
    {
        test_fail("No external semaphore handle types found\n");
    }

    for (VulkanExternalSemaphoreHandleType vkExternalSemaphoreHandleType :
         vkExternalSemaphoreHandleTypeList)
    {
        log_info_semaphore_type(vkExternalSemaphoreHandleType);
        VulkanSemaphore vkVk2CLSemaphore1(vkDevice,
                                          vkExternalSemaphoreHandleType);
        VulkanSemaphore vkVk2CLSemaphore2(vkDevice,
                                          vkExternalSemaphoreHandleType);

        auto sema_ext_1 = clExternalImportableSemaphore(
            vkVk2CLSemaphore1, context, vkExternalSemaphoreHandleType, device);

        auto sema_ext_2 = clExternalImportableSemaphore(
            vkVk2CLSemaphore2, context, vkExternalSemaphoreHandleType, device);

        cl_int err = CL_SUCCESS;

        // Create ooo queue
        clCommandQueueWrapper queue = clCreateCommandQueue(
            context, device, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
        test_error(err, "Could not create command queue");

        // Signal semaphore 1 and 2
        clEventWrapper signal_event;
        cl_semaphore_khr sema_list[] = { sema_ext_1.getCLSemaphore(),
                                         sema_ext_2.getCLSemaphore() };
        err = clEnqueueSignalSemaphoresKHR(queue, 2, sema_list, nullptr, 0,
                                           nullptr, &signal_event);
        test_error(err, "Could not signal semaphore");

        // Wait semaphore 1
        clEventWrapper wait_1_event;
        err = clEnqueueWaitSemaphoresKHR(queue, 1, &sema_ext_1.getCLSemaphore(),
                                         nullptr, 0, nullptr, &wait_1_event);
        test_error(err, "Could not wait semaphore");

        // Wait semaphore 2
        clEventWrapper wait_2_event;
        err = clEnqueueWaitSemaphoresKHR(queue, 1, &sema_ext_2.getCLSemaphore(),
                                         nullptr, 0, nullptr, &wait_2_event);
        test_error(err, "Could not wait semaphore");

        // Finish
        err = clFinish(queue);
        test_error(err, "Could not finish queue");

        // Ensure all events are completed
        test_assert_event_complete(signal_event);
        test_assert_event_complete(wait_1_event);
        test_assert_event_complete(wait_2_event);
    }

    return TEST_PASS;
}

// Confirm that we can wait for multiple semaphores with one command
REGISTER_TEST_VERSION(external_semaphores_multi_wait, Version(1, 2))
{
    REQUIRE_EXTENSION("cl_khr_external_semaphore");

    if (init_vulkan_device(1, &device))
    {
        log_info("Cannot initialise Vulkan. "
                 "Skipping test.\n");
        return TEST_SKIPPED_ITSELF;
    }

    VulkanDevice vkDevice;

    // Obtain pointers to semaphore's API
    GET_PFN(device, clEnqueueSignalSemaphoresKHR);
    GET_PFN(device, clEnqueueWaitSemaphoresKHR);

    std::vector<VulkanExternalSemaphoreHandleType>
        vkExternalSemaphoreHandleTypeList =
            getSupportedInteropExternalSemaphoreHandleTypes(device, vkDevice);

    if (vkExternalSemaphoreHandleTypeList.empty())
    {
        test_fail("No external semaphore handle types found\n");
    }

    for (VulkanExternalSemaphoreHandleType vkExternalSemaphoreHandleType :
         vkExternalSemaphoreHandleTypeList)
    {
        log_info_semaphore_type(vkExternalSemaphoreHandleType);
        VulkanSemaphore vkVk2CLSemaphore1(vkDevice,
                                          vkExternalSemaphoreHandleType);
        VulkanSemaphore vkVk2CLSemaphore2(vkDevice,
                                          vkExternalSemaphoreHandleType);

        auto sema_ext_1 = clExternalImportableSemaphore(
            vkVk2CLSemaphore1, context, vkExternalSemaphoreHandleType, device);

        auto sema_ext_2 = clExternalImportableSemaphore(
            vkVk2CLSemaphore2, context, vkExternalSemaphoreHandleType, device);

        cl_int err = CL_SUCCESS;

        // Create ooo queue
        clCommandQueueWrapper queue = clCreateCommandQueue(
            context, device, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &err);
        test_error(err, "Could not create command queue");

        // Signal semaphore 1
        clEventWrapper signal_1_event;
        err =
            clEnqueueSignalSemaphoresKHR(queue, 1, &sema_ext_1.getCLSemaphore(),
                                         nullptr, 0, nullptr, &signal_1_event);
        test_error(err, "Could not signal semaphore");

        // Signal semaphore 2
        clEventWrapper signal_2_event;
        err =
            clEnqueueSignalSemaphoresKHR(queue, 1, &sema_ext_2.getCLSemaphore(),
                                         nullptr, 0, nullptr, &signal_2_event);
        test_error(err, "Could not signal semaphore");

        // Wait semaphore 1 and 2
        clEventWrapper wait_event;
        cl_semaphore_khr sema_list[] = { sema_ext_1.getCLSemaphore(),
                                         sema_ext_2.getCLSemaphore() };
        err = clEnqueueWaitSemaphoresKHR(queue, 2, sema_list, nullptr, 0,
                                         nullptr, &wait_event);
        test_error(err, "Could not wait semaphore");

        // Finish
        err = clFinish(queue);
        test_error(err, "Could not finish queue");

        // Ensure all events are completed
        test_assert_event_complete(signal_1_event);
        test_assert_event_complete(signal_2_event);
        test_assert_event_complete(wait_event);
    }

    return TEST_PASS;
}
