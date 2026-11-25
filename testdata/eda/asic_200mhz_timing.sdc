# Synopsys Timing Constraints for RISC-V SoC @ 200 MHz + 50 MHz SPI
create_clock -name core_clk -period 5.0 [get_ports clk_core_i]
create_clock -name spi_clk  -period 20.0 [get_ports spi_sck_i]
set_input_delay -clock core_clk 1.65 [get_ports axi_rdata_i]
set_output_delay -clock core_clk 2.10 [get_ports axi_wdata_o]
set_input_delay -clock spi_clk 4.50 [get_ports spi_miso_i]
set_false_path -from [get_ports rst_async_ni]
set_multicycle_path 2 -setup -from [get_pins u_csr/*]