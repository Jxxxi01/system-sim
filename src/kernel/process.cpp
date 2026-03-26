#include "kernel/process.hpp"

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sim::kernel {
namespace {

std::string MakeContextSwitchDetail(const ProcessContext& process) {
  std::ostringstream oss;
  oss << "base_va=" << process.base_va;
  return oss.str();
}

}  // namespace

KernelProcessTable::KernelProcessTable(sim::security::Gateway& gateway, sim::security::SecurityHardware& hardware,
                                       sim::security::AuditCollector& audit)
    : gateway_(gateway), hardware_(hardware), audit_(audit) {}

sim::security::ContextHandle KernelProcessTable::LoadProcess(sim::security::SecureIrPackage package) {
  sim::security::GatewayLoadResult load_result;
  bool gateway_loaded = false;
  std::vector<std::uint64_t> registered_page_ids;

  try {
    load_result = gateway_.Load(std::move(package));
    gateway_loaded = true;

    for (const sim::security::GatewayPageLayout& page : load_result.pages) {
      const sim::security::PvtRegisterResult register_result =
          hardware_.GetPvtTable().RegisterPage(load_result.handle, page.va, page.page_type);
      if (!register_result.ok) {
        throw std::runtime_error(register_result.error);
      }
      registered_page_ids.push_back(register_result.pa_page_id);
    }

    ProcessContext process;
    process.context_handle = load_result.handle;
    process.user_id = load_result.user_id;
    process.base_va = load_result.base_va;
    process.pvt_page_ids = registered_page_ids;

    const auto inserted = processes_.emplace(process.context_handle, std::move(process));
    if (!inserted.second) {
      std::ostringstream oss;
      oss << "kernel_process_duplicate_handle context_handle=" << load_result.handle;
      throw std::runtime_error(oss.str());
    }

    return load_result.handle;
  } catch (...) {
    for (std::uint64_t pa_page_id : registered_page_ids) {
      hardware_.GetPvtTable().RemovePage(pa_page_id);
    }
    if (load_result.handle != 0) {
      processes_.erase(load_result.handle);
      if (active_handle_.has_value() && *active_handle_ == load_result.handle) {
        active_handle_.reset();
        hardware_.ClearActiveHandle();
      }
      if (gateway_loaded) {
        gateway_.Release(load_result.handle);
      }
    }
    throw;
  }
}

void KernelProcessTable::ReleaseProcess(sim::security::ContextHandle handle) {
  const bool was_active = active_handle_.has_value() && *active_handle_ == handle;
  const ProcessContext* process = GetProcess(handle);
  if (process != nullptr) {
    for (std::uint64_t pa_page_id : process->pvt_page_ids) {
      hardware_.GetPvtTable().RemovePage(pa_page_id);
    }
  }
  gateway_.Release(handle);
  processes_.erase(handle);
  if (was_active) {
    active_handle_.reset();
    hardware_.ClearActiveHandle();
  }
}

void KernelProcessTable::ContextSwitch(sim::security::ContextHandle handle) {
  const ProcessContext* process = GetProcess(handle);
  if (process == nullptr) {
    std::ostringstream oss;
    oss << "kernel_process_invalid_handle context_handle=" << handle;
    throw std::runtime_error(oss.str());
  }

  hardware_.SetActiveHandle(handle);
  active_handle_ = handle;
  audit_.LogEvent("CTX_SWITCH", process->user_id, process->context_handle, process->base_va,
                  MakeContextSwitchDetail(*process));
}

const ProcessContext* KernelProcessTable::GetActiveProcess() const {
  if (!active_handle_.has_value()) {
    return nullptr;
  }
  return GetProcess(*active_handle_);
}

const ProcessContext* KernelProcessTable::GetProcess(sim::security::ContextHandle handle) const {
  const auto it = processes_.find(handle);
  if (it == processes_.end()) {
    return nullptr;
  }
  return &it->second;
}

}  // namespace sim::kernel
