# SPDX-License-Identifier: GPL-2.0
dtb-$(CONFIG_JZ4740_QI_LB60)	+= qi_lb60.dtb
dtb-$(CONFIG_JZ4770_GCW0)	+= gcw0.dtb
dtb-$(CONFIG_JZ4780_CI20)	+= ci20.dtb
dtb-$(CONFIG_DT_HALLEY2_V20)	+= halley2v20.dtb
dtb-$(CONFIG_DT_SEAL)	+= seal.dtb
dtb-$(CONFIG_DT_X2000_V12_FPGA)	+= x2000_v12_fpga.dtb
dtb-$(CONFIG_DT_T40_SHARK)	+= shark.dtb
dtb-$(CONFIG_DT_T31_SWAN)	+= swan.dtb

# DTS has been tested on corresponding devices
dtb-$(CONFIG_DT_T31L_CINNADO_D1)	+= t31l_cinnado_d1.dtb
dtb-$(CONFIG_DT_T31X_WYZE_V3)		+= t31x_wyze_v3.dtb

# Experimental Config for prototyping
dtb-$(CONFIG_DT_T31_PERSONALCAM)	+= personalcam.dtb

obj-y				+= $(patsubst %.dtb, %.dtb.o, $(dtb-y))

# Force kbuild to make empty built-in.o if necessary
obj-				+= dummy.o

always				:= $(dtb-y)
clean-files			:= *.dtb *.dtb.S
