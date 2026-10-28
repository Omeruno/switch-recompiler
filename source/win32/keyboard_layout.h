#pragma once
#include <cstdint>
#include <string>
namespace win32::keyboard {
// Fixed US 101/102-key profile, matching USER GetKeyboardLayout 04090409.
uint32_t Map(uint32_t code,uint32_t type);
uint16_t PhysicalScan(uint32_t vk);
uint32_t Generic(uint32_t vk);
uint32_t PhysicalKey(uint32_t vk,uint16_t scan);
uint32_t MessageScan(uint16_t scan); // WM_KEY* scan/extended bits only
std::string Name(uint32_t lparam);
}
