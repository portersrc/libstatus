#include <fstream>
#include <assert.h>
#include "system.h"

// FIXME don't assert in library

GPUSystem::GPUSystem(std::string filename): fname(filename), loop(false) {
    unsigned int num_gpus;

    NVML_RT_CALL(nvmlInit());

    // Query the number of GPUs.
    NVML_RT_CALL(nvmlDeviceGetCount(&num_gpus));

    // for each mig device across all GPUs, create a GPUDevice object
    for (int i = 0; i < num_gpus; i++) {

        int device_id = i;
        int device_name_length = 64;
        char name[device_name_length]; // device name
        nvmlReturn_t rc;
        unsigned int current_mode;
        unsigned int pending_mode;
        unsigned int max_mig_device_count;
        int mig_idx = 0;

        nvmlDevice_t device; // device handle
        NVML_RT_CALL(nvmlDeviceGetHandleByIndex_v2(device_id, &device));
        NVML_RT_CALL(nvmlDeviceGetName(device, name, device_name_length));
        std::cout << "Device " << device_id << "(" << device << ") : " << name << "\n";

        rc = nvmlDeviceGetMigMode(device, &current_mode, &pending_mode);
        assert(rc == NVML_SUCCESS);
        assert(current_mode == NVML_DEVICE_MIG_ENABLE);

        rc = nvmlDeviceGetMaxMigDeviceCount(device, &max_mig_device_count);
        assert(rc == NVML_SUCCESS);
        std::cout << "  max mig device count: " << max_mig_device_count << "\n";

        while (mig_idx < max_mig_device_count) {
            nvmlDevice_t mig_device;
            unsigned int is_mig_device;
            rc = nvmlDeviceGetMigDeviceHandleByIndex(device, mig_idx, &mig_device);
            if (rc != NVML_SUCCESS) {
                // Case: I assume this happens when we fail to get the mig
                // device at some index beyond the valid index, but before
                // the max device count. I'm not sure why there is no API call
                // for the number of mig devices. I only see a call for the max
                // number. For the 4xA30s exercise on quorra1, this condition
                // doesn't seem to get hit (I guess because we use the max
                // number of slices).
                break;
            }
            assert(rc == NVML_SUCCESS);

            rc = nvmlDeviceIsMigDeviceHandle(mig_device, &is_mig_device); // sanity check
            assert(rc == NVML_SUCCESS);
            assert(is_mig_device == 1);

            std::cout << "  adding a mig device\n";
            devices.push_back(new GPUDevice(mig_device));

            mig_idx++;
        }

        std::cout << "  added " << mig_idx << " mig devices\n";
    }
    std::cout << "total size of devices vector: " << devices.size() << "\n";
    num_devices = devices.size();

}

GPUSystem::~GPUSystem() {
    for (int i = 0; i < num_devices; i++) {
        delete devices[i];
    } 
    NVML_RT_CALL(nvmlShutdown());
}

void GPUSystem::start() {
    loop = true;
    while(loop) {
        std::time_t stamp = std::chrono::high_resolution_clock::now().time_since_epoch( ).count();
        timestamps.push_back(stamp);
        for (int i = 0; i < num_devices; i++) {
            devices[i]->query();
            // print for debug
            //nvmlUtilization_t u = devices[i]->get_utilization();
            //std::cout << "Device " << i 
            //          << ": GPU utilization: " << u.gpu 
            //          << ", MEM Utilization: " << u.memory << "\n";
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
}

void GPUSystem::stop() {
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) ); // Retrive a few empty samples
    loop = false;
}

void GPUSystem::print_header(std::ofstream &ofs) {
    ofs << "timestamp";
    for (int i = 0; i < num_devices; i++) ofs << ",device_" << i;
    ofs << "\n";
}

void GPUSystem::dump() {
    std::ofstream core_util_file(fname+"_gpu.csv", std::ios::out);
    std::ofstream mem_util_file(fname+"_mem.csv", std::ios::out);

    print_header(core_util_file);
    print_header(mem_util_file);

    for(int i = 0; i < timestamps.size(); i++) {
        core_util_file << timestamps[i];
        mem_util_file << timestamps[i];
        for (int j = 0; j < num_devices; j++) {
            nvmlUtilization_t u = devices[j]->get_utilization(i);
            core_util_file << "," << u.gpu;
            mem_util_file << "," << u.memory;
        }
        core_util_file << "\n";
        mem_util_file << "\n";
    }
    core_util_file.close();
    mem_util_file.close();
}
