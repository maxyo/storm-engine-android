Run `python3 tests/service_init/run.py --build-dir build/service-init-test` with a C++20 compiler supporting ASan/UBSan.

The harness compiles the actual `CoreImpl::GetService` implementation with the production singleton registration macro and a fault-injected service. It checks false and throwing initialization, automatic exceptions, failed-instance destruction, retries, and reuse/registration of successful instances. `--source-root` can point to a baseline checkout. This host test does not exercise Vulkan or the Android SDL error dialog; those require a device test.
