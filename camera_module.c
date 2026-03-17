#include "address_map.h"

int main(void) {
  // Pointers to video-in DMA controller registers
  volatile int* video_in_dma = (int*)VIDEO_IN_BASE;         // 0xFF203060
  volatile int* edge_detect = (int*)EDGE_DETECT_CTRL_BASE;  // 0xFF203070

  // Pointer to pixel buffer controller (video-out)
  volatile int* pixel_buf_ctrl = (int*)PIXEL_CTRL_BASE;  // 0xFF203020

  // Step 1: Set both video-in DMA and video-out to use the same buffer
  // Default buffer address is 0x08000000 (FPGA on-chip memory)
  // This means video-in writes directly to what video-out displays
  *(video_in_dma + 1) = 0x08000000;  // set BackBuffer to on-chip memory

  // Step 2: Disable edge detection (normal image, not edge-detected)
  *edge_detect = 0x0;

  // Step 3: Enable the Video DMA controller (set EN bit)
  // Control register is at 0xFF20306C (offset 3 from VIDEO_IN_BASE)
  *(video_in_dma + 3) = 0x4;  // EN = 1

  // Step 4: Enable the pixel buffer controller (video-out)
  // Writing 1 to Buffer register triggers display
  *pixel_buf_ctrl = 1;

  // Step 5: Loop forever - hardware handles the rest
  // The DMA continuously writes camera frames to the pixel buffer
  // and the VGA controller continuously reads and displays them
  while (1) {
    // Nothing needed here - it's all done in hardware
    // Optionally you could add KEY press handling here,
    // e.g. toggle edge detection on/off
  }

  return 0;
}
