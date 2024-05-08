//
// Created by matteius on 5/7/24.
//

#ifndef INGENIC_T31_ZRT_KERNEL_4_4_94_MATTEIUS_ISP_MEM_MAP_H
#define INGENIC_T31_ZRT_KERNEL_4_4_94_MATTEIUS_ISP_MEM_MAP_H

volatile void __iomem *get_isp_base(void);
volatile struct resource *get_isp_res(void);

#endif //INGENIC_T31_ZRT_KERNEL_4_4_94_MATTEIUS_ISP_MEM_MAP_H
