set(inc "Include" "cmsis-core")

set(src 
			"Source/DAP.c"
			"Source/DAP_vendor.c"
			"Source/JTAG_DP.c"
			"Source/SW_DP.c"
			"Source/swd_host.c"
			"Source/error.c"
			)


idf_component_register(SRCS ${src}
                       INCLUDE_DIRS ${inc}
                       REQUIRES driver
                       )
