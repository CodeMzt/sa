/* generated vector header file - do not edit */
#ifndef VECTOR_DATA_H
#define VECTOR_DATA_H
#ifdef __cplusplus
        extern "C" {
        #endif
/* Number of interrupts allocated */
#ifndef VECTOR_DATA_IRQ_COUNT
#define VECTOR_DATA_IRQ_COUNT    (24)
#endif
/* ISR prototypes */
void canfd_error_isr(void);
void canfd_channel_tx_isr(void);
void canfd_common_fifo_rx_isr(void);
void canfd_rx_fifo_isr(void);
void ether_eint_isr(void);
void sci_uart_rxi_isr(void);
void sci_uart_txi_isr(void);
void sci_uart_tei_isr(void);
void sci_uart_eri_isr(void);
void spi_rxi_isr(void);
void spi_tei_isr(void);
void spi_eri_isr(void);
void dmac_int_isr(void);
void iic_master_rxi_isr(void);
void iic_master_txi_isr(void);
void iic_master_tei_isr(void);
void iic_master_eri_isr(void);
void ssi_rxi_isr(void);
void ssi_int_isr(void);

/* Vector table allocations */
#define VECTOR_NUMBER_CAN0_CHERR ((IRQn_Type) 0) /* CAN0 CHERR (Channel  error) */
#define CAN0_CHERR_IRQn          ((IRQn_Type) 0) /* CAN0 CHERR (Channel  error) */
#define VECTOR_NUMBER_CAN0_TX ((IRQn_Type) 1) /* CAN0 TX (Transmit interrupt) */
#define CAN0_TX_IRQn          ((IRQn_Type) 1) /* CAN0 TX (Transmit interrupt) */
#define VECTOR_NUMBER_CAN0_COMFRX ((IRQn_Type) 2) /* CAN0 COMFRX (Common FIFO receive interrupt) */
#define CAN0_COMFRX_IRQn          ((IRQn_Type) 2) /* CAN0 COMFRX (Common FIFO receive interrupt) */
#define VECTOR_NUMBER_CAN_GLERR ((IRQn_Type) 3) /* CAN GLERR (Global error) */
#define CAN_GLERR_IRQn          ((IRQn_Type) 3) /* CAN GLERR (Global error) */
#define VECTOR_NUMBER_CAN_RXF ((IRQn_Type) 4) /* CAN RXF (Global receive FIFO interrupt) */
#define CAN_RXF_IRQn          ((IRQn_Type) 4) /* CAN RXF (Global receive FIFO interrupt) */
#define VECTOR_NUMBER_EDMAC0_EINT ((IRQn_Type) 5) /* EDMAC0 EINT (EDMAC 0 interrupt) */
#define EDMAC0_EINT_IRQn          ((IRQn_Type) 5) /* EDMAC0 EINT (EDMAC 0 interrupt) */
#define VECTOR_NUMBER_SCI7_RXI ((IRQn_Type) 6) /* SCI7 RXI (Receive data full) */
#define SCI7_RXI_IRQn          ((IRQn_Type) 6) /* SCI7 RXI (Receive data full) */
#define VECTOR_NUMBER_SCI7_TXI ((IRQn_Type) 7) /* SCI7 TXI (Transmit data empty) */
#define SCI7_TXI_IRQn          ((IRQn_Type) 7) /* SCI7 TXI (Transmit data empty) */
#define VECTOR_NUMBER_SCI7_TEI ((IRQn_Type) 8) /* SCI7 TEI (Transmit end) */
#define SCI7_TEI_IRQn          ((IRQn_Type) 8) /* SCI7 TEI (Transmit end) */
#define VECTOR_NUMBER_SCI7_ERI ((IRQn_Type) 9) /* SCI7 ERI (Receive error) */
#define SCI7_ERI_IRQn          ((IRQn_Type) 9) /* SCI7 ERI (Receive error) */
#define VECTOR_NUMBER_SPI1_RXI ((IRQn_Type) 10) /* SPI1 RXI (Receive buffer full) */
#define SPI1_RXI_IRQn          ((IRQn_Type) 10) /* SPI1 RXI (Receive buffer full) */
#define VECTOR_NUMBER_SPI1_TEI ((IRQn_Type) 11) /* SPI1 TEI (Transmission complete event) */
#define SPI1_TEI_IRQn          ((IRQn_Type) 11) /* SPI1 TEI (Transmission complete event) */
#define VECTOR_NUMBER_SPI1_ERI ((IRQn_Type) 12) /* SPI1 ERI (Error) */
#define SPI1_ERI_IRQn          ((IRQn_Type) 12) /* SPI1 ERI (Error) */
#define VECTOR_NUMBER_DMAC1_INT ((IRQn_Type) 13) /* DMAC1 INT (DMAC1 transfer end) */
#define DMAC1_INT_IRQn          ((IRQn_Type) 13) /* DMAC1 INT (DMAC1 transfer end) */
#define VECTOR_NUMBER_IIC2_RXI ((IRQn_Type) 14) /* IIC2 RXI (Receive data full) */
#define IIC2_RXI_IRQn          ((IRQn_Type) 14) /* IIC2 RXI (Receive data full) */
#define VECTOR_NUMBER_IIC2_TXI ((IRQn_Type) 15) /* IIC2 TXI (Transmit data empty) */
#define IIC2_TXI_IRQn          ((IRQn_Type) 15) /* IIC2 TXI (Transmit data empty) */
#define VECTOR_NUMBER_IIC2_TEI ((IRQn_Type) 16) /* IIC2 TEI (Transmit end) */
#define IIC2_TEI_IRQn          ((IRQn_Type) 16) /* IIC2 TEI (Transmit end) */
#define VECTOR_NUMBER_IIC2_ERI ((IRQn_Type) 17) /* IIC2 ERI (Transfer error) */
#define IIC2_ERI_IRQn          ((IRQn_Type) 17) /* IIC2 ERI (Transfer error) */
#define VECTOR_NUMBER_SCI6_RXI ((IRQn_Type) 18) /* SCI6 RXI (Receive data full) */
#define SCI6_RXI_IRQn          ((IRQn_Type) 18) /* SCI6 RXI (Receive data full) */
#define VECTOR_NUMBER_SCI6_TXI ((IRQn_Type) 19) /* SCI6 TXI (Transmit data empty) */
#define SCI6_TXI_IRQn          ((IRQn_Type) 19) /* SCI6 TXI (Transmit data empty) */
#define VECTOR_NUMBER_SCI6_TEI ((IRQn_Type) 20) /* SCI6 TEI (Transmit end) */
#define SCI6_TEI_IRQn          ((IRQn_Type) 20) /* SCI6 TEI (Transmit end) */
#define VECTOR_NUMBER_SCI6_ERI ((IRQn_Type) 21) /* SCI6 ERI (Receive error) */
#define SCI6_ERI_IRQn          ((IRQn_Type) 21) /* SCI6 ERI (Receive error) */
#define VECTOR_NUMBER_SSI0_RXI ((IRQn_Type) 22) /* SSI0 RXI (Receive data full) */
#define SSI0_RXI_IRQn          ((IRQn_Type) 22) /* SSI0 RXI (Receive data full) */
#define VECTOR_NUMBER_SSI0_INT ((IRQn_Type) 23) /* SSI0 INT (Error interrupt) */
#define SSI0_INT_IRQn          ((IRQn_Type) 23) /* SSI0 INT (Error interrupt) */
/* The number of entries required for the ICU vector table. */
#define BSP_ICU_VECTOR_NUM_ENTRIES (24)

#ifdef __cplusplus
        }
        #endif
#endif /* VECTOR_DATA_H */
