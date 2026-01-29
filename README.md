A very rough, work in progress pure ESP-IDF driver for Waveshare 2.15in Hat G e-Paper display. It is graphics API agnostic (operates on a DMA able framebuffer).

It is currently not complete, has known bugs, evolves rapidly and the API is not ready for consumption. It does not free resources it uses consistently yet and will likely fail when cache is not available as not everything is correctly put in IRAM / DRAM capable sections.

The goal is to eventually fix all these issues and more.