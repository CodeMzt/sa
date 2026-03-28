/* generated vector source file - do not edit */
#include "bsp_api.h"
/* Do not build these data structures if no interrupts are currently allocated because IAR will have build errors. */
#if VECTOR_DATA_IRQ_COUNT > 0
        BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_NUM_ENTRIES] BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
        {
                        [0] = canfd_error_isr, /* CAN0 CHERR (Channel  error) */
            [1] = canfd_channel_tx_isr, /* CAN0 TX (Transmit interrupt) */
            [2] = canfd_common_fifo_rx_isr, /* CAN0 COMFRX (Common FIFO receive interrupt) */
            [3] = canfd_error_isr, /* CAN GLERR (Global error) */
            [4] = canfd_rx_fifo_isr, /* CAN RXF (Global receive FIFO interrupt) */
            [5] = ether_eint_isr, /* EDMAC0 EINT (EDMAC 0 interrupt) */
            [6] = sci_uart_rxi_isr, /* SCI7 RXI (Receive data full) */
            [7] = sci_uart_txi_isr, /* SCI7 TXI (Transmit data empty) */
            [8] = sci_uart_tei_isr, /* SCI7 TEI (Transmit end) */
            [9] = sci_uart_eri_isr, /* SCI7 ERI (Receive error) */
            [10] = spi_tei_isr, /* SPI1 TEI (Transmission complete event) */
            [11] = spi_eri_isr, /* SPI1 ERI (Error) */
            [12] = dmac_int_isr, /* DMAC1 INT (DMAC1 transfer end) */
            [13] = dmac_int_isr, /* DMAC0 INT (DMAC0 transfer end) */
            [14] = iic_master_rxi_isr, /* IIC2 RXI (Receive data full) */
            [15] = iic_master_txi_isr, /* IIC2 TXI (Transmit data empty) */
            [16] = iic_master_tei_isr, /* IIC2 TEI (Transmit end) */
            [17] = iic_master_eri_isr, /* IIC2 ERI (Transfer error) */
            [18] = sci_uart_rxi_isr, /* SCI6 RXI (Receive data full) */
            [19] = sci_uart_txi_isr, /* SCI6 TXI (Transmit data empty) */
            [20] = sci_uart_tei_isr, /* SCI6 TEI (Transmit end) */
            [21] = sci_uart_eri_isr, /* SCI6 ERI (Receive error) */
            [22] = ssi_rxi_isr, /* SSI0 RXI (Receive data full) */
            [23] = ssi_int_isr, /* SSI0 INT (Error interrupt) */
            [24] = sci_uart_rxi_isr, /* SCI9 RXI (Receive data full) */
            [25] = sci_uart_txi_isr, /* SCI9 TXI (Transmit data empty) */
            [26] = sci_uart_tei_isr, /* SCI9 TEI (Transmit end) */
            [27] = sci_uart_eri_isr, /* SCI9 ERI (Receive error) */
            [28] = sci_i2c_txi_isr, /* SCI4 TXI (Transmit data empty) */
            [29] = sci_i2c_tei_isr, /* SCI4 TEI (Transmit end) */
        };
        #if BSP_FEATURE_ICU_HAS_IELSR
        const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_NUM_ENTRIES] =
        {
            [0] = BSP_PRV_VECT_ENUM(EVENT_CAN0_CHERR,GROUP0), /* CAN0 CHERR (Channel  error) */
            [1] = BSP_PRV_VECT_ENUM(EVENT_CAN0_TX,GROUP1), /* CAN0 TX (Transmit interrupt) */
            [2] = BSP_PRV_VECT_ENUM(EVENT_CAN0_COMFRX,GROUP2), /* CAN0 COMFRX (Common FIFO receive interrupt) */
            [3] = BSP_PRV_VECT_ENUM(EVENT_CAN_GLERR,GROUP3), /* CAN GLERR (Global error) */
            [4] = BSP_PRV_VECT_ENUM(EVENT_CAN_RXF,GROUP4), /* CAN RXF (Global receive FIFO interrupt) */
            [5] = BSP_PRV_VECT_ENUM(EVENT_EDMAC0_EINT,GROUP5), /* EDMAC0 EINT (EDMAC 0 interrupt) */
            [6] = BSP_PRV_VECT_ENUM(EVENT_SCI7_RXI,GROUP6), /* SCI7 RXI (Receive data full) */
            [7] = BSP_PRV_VECT_ENUM(EVENT_SCI7_TXI,GROUP7), /* SCI7 TXI (Transmit data empty) */
            [8] = BSP_PRV_VECT_ENUM(EVENT_SCI7_TEI,GROUP0), /* SCI7 TEI (Transmit end) */
            [9] = BSP_PRV_VECT_ENUM(EVENT_SCI7_ERI,GROUP1), /* SCI7 ERI (Receive error) */
            [10] = BSP_PRV_VECT_ENUM(EVENT_SPI1_TEI,GROUP2), /* SPI1 TEI (Transmission complete event) */
            [11] = BSP_PRV_VECT_ENUM(EVENT_SPI1_ERI,GROUP3), /* SPI1 ERI (Error) */
            [12] = BSP_PRV_VECT_ENUM(EVENT_DMAC1_INT,GROUP4), /* DMAC1 INT (DMAC1 transfer end) */
            [13] = BSP_PRV_VECT_ENUM(EVENT_DMAC0_INT,GROUP5), /* DMAC0 INT (DMAC0 transfer end) */
            [14] = BSP_PRV_VECT_ENUM(EVENT_IIC2_RXI,GROUP6), /* IIC2 RXI (Receive data full) */
            [15] = BSP_PRV_VECT_ENUM(EVENT_IIC2_TXI,GROUP7), /* IIC2 TXI (Transmit data empty) */
            [16] = BSP_PRV_VECT_ENUM(EVENT_IIC2_TEI,GROUP0), /* IIC2 TEI (Transmit end) */
            [17] = BSP_PRV_VECT_ENUM(EVENT_IIC2_ERI,GROUP1), /* IIC2 ERI (Transfer error) */
            [18] = BSP_PRV_VECT_ENUM(EVENT_SCI6_RXI,GROUP2), /* SCI6 RXI (Receive data full) */
            [19] = BSP_PRV_VECT_ENUM(EVENT_SCI6_TXI,GROUP3), /* SCI6 TXI (Transmit data empty) */
            [20] = BSP_PRV_VECT_ENUM(EVENT_SCI6_TEI,GROUP4), /* SCI6 TEI (Transmit end) */
            [21] = BSP_PRV_VECT_ENUM(EVENT_SCI6_ERI,GROUP5), /* SCI6 ERI (Receive error) */
            [22] = BSP_PRV_VECT_ENUM(EVENT_SSI0_RXI,GROUP6), /* SSI0 RXI (Receive data full) */
            [23] = BSP_PRV_VECT_ENUM(EVENT_SSI0_INT,GROUP7), /* SSI0 INT (Error interrupt) */
            [24] = BSP_PRV_VECT_ENUM(EVENT_SCI9_RXI,GROUP0), /* SCI9 RXI (Receive data full) */
            [25] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TXI,GROUP1), /* SCI9 TXI (Transmit data empty) */
            [26] = BSP_PRV_VECT_ENUM(EVENT_SCI9_TEI,GROUP2), /* SCI9 TEI (Transmit end) */
            [27] = BSP_PRV_VECT_ENUM(EVENT_SCI9_ERI,GROUP3), /* SCI9 ERI (Receive error) */
            [28] = BSP_PRV_VECT_ENUM(EVENT_SCI4_TXI,GROUP4), /* SCI4 TXI (Transmit data empty) */
            [29] = BSP_PRV_VECT_ENUM(EVENT_SCI4_TEI,GROUP5), /* SCI4 TEI (Transmit end) */
        };
        #endif
        #endif
