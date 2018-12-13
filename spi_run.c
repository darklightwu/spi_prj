#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <bcm2835.h>
#include <wiringPi.h>
#include "lib/debug.h"
#include "data_st.h"
#include "spi_run.h"

//#define   SPI_TXRX_LOOP_TEST       //for loop test

#ifndef   SPI_MULTI_SLAVE_INT
#define   SPI_SINLE_SLAVE_INT_LINE 1
#endif

#define   SPI_SLAVE_INT_HANDLE(x)  (spi_slave##x##_isr_handle) 
#define   SPI_SLAVE_ISR_SIZE (SPI_SLAVE_NUM/5?5:9) //why 5 or 9 ? because slave_isr_cnt[0] not used

static int slave_isr_cnt[SPI_SLAVE_ISR_SIZE] = {0,100000000};

static unsigned char spi_init_done = 0;
static void spi_end_handle(int param);

static int exit_signal_register(void)
{
  if (signal(SIGINT, spi_end_handle) == SIG_ERR) {
    perror("signal");
    return 1;
  }

  return 0;
}

static int spi_init(void)
{
  if (!bcm2835_init())
  {
   printf("bcm2835_init failed. Are you running as root??\n");
   return 1;
  }

  if (!bcm2835_spi_begin())  {
   printf("bcm2835_spi_begin failed. Are you running as root??\n");
   return 1;
  }

  bcm2835_spi_begin();
  bcm2835_spi_setBitOrder(BCM2835_SPI_BIT_ORDER_MSBFIRST);   // The default

  bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);          // The default
  bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_32);
  bcm2835_spi_chipSelect(BCM2835_SPI_CS0);          
  bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS0, LOW);   // the default
	
  spi_init_done = 1;

  return 0;
}

static void gpio_ext_int_init(void)
{
  wiringPiSetup () ;

  /*GPIO 0-7 used for external interrupt handler*/
  wiringPiISR (0, INT_EDGE_FALLING, SPI_SLAVE_INT_HANDLE(1)) ;
#if defined SPI_MULTI_SLAVE_INT
  wiringPiISR (1, INT_EDGE_FALLING, SPI_SLAVE_INT_HANDLE(2)) ;
  wiringPiISR (2, INT_EDGE_FALLING, SPI_SLAVE_INT_HANDLE(3)) ;
  wiringPiISR (3, INT_EDGE_FALLING, SPI_SLAVE_INT_HANDLE(4)) ;
  //TODO
#endif
}

int spi_run(void *arg)
{
  uint8_t *spi_initialized = (uint8_t *)arg;

  if(exit_signal_register()) return 1;

  gpio_ext_int_init();

  if(spi_init() != 0)
    return 1;

  *spi_initialized = 1;

  while(1){
    slave_id_t sid = 0;       //slave id 0 is invalid
    slave_id_t recv_from = 0; //slave id 0 is invalid
#ifdef SPI_MULTI_SLAVE_INT
    static slave_id_t next_recv_sid = 1; //next_recv_sid:1-4(8),initialized to 1
#endif
    char  tx_buf[BUF_SIZE] = { 0x01, 0x02, 0x11, 0x33, 0x22,0x44,0x55,0x66,0x77,0x88,0x99 }; // Data to send
    char  rx_buf[BUF_SIZE] = { 0x07, 0x08, 0x77, 0x88 }; // Data to receive

    sid = spi_tx_data_out((uint8_t*)tx_buf,sizeof(tx_buf));//sid: 1-4(8)
    //if(sid)
     //dump_debug_log("spi_tx",tx_buf,sizeof(tx_buf));
#ifndef SPI_TXRX_LOOP_TEST
  #ifdef SPI_MULTI_SLAVE_INT
    /**********************************************************************************************
     ***************************Multi Intterrupt Lines(1-8) Prorssing *****************************
     **********************************************************************************************/
    if(sid && slave_isr_cnt[sid]){  //need to send and receive
      //TODO pull GPIO(sid) low 
      bcm2835_spi_transfernb(tx_buf, rx_buf, sizeof(tx_buf));
      //TODO pull GPIO(sid) High
      slave_isr_cnt[sid]--;
      if(sid == next_recv_sid)//sid: 1-4(8) and next_recv_sid:1-4(8)
        next_recv_sid = next_recv_sid % SPI_SLAVE_NUM + 1;
      recv_from = sid;             //recv from:1-4(8)  sid: 1-4(8)
    }else if(sid){                 //just to send
      //TODO pull GPIO(sid) low 
      bcm2835_spi_writenb(tx_buf, sizeof(tx_buf));
      //TODO pull GPIO(sid) High
    }else{                        //just to receive according to the isr event and count
      if(slave_isr_cnt[next_recv_sid]){
        memset(tx_buf,0xff,sizeof(tx_buf));
        //TODO pull GPIO(next_recv_sid) low
        bcm2835_spi_transfernb(tx_buf, rx_buf, sizeof(tx_buf));
        //TODO pull GPIO(next_recv_sid) High
        slave_isr_cnt[next_recv_sid]--;
        recv_from = next_recv_sid;  //recv from:1-4(8)  next_recv_sid:1-4(8)
      }
      next_recv_sid = next_recv_sid % SPI_SLAVE_NUM + 1;
    }
  #else
    /**********************************************************************************************
     *************************Single Intterrupt Line(Only 1) Prorssing ****************************
     **********************************************************************************************/
    if(sid && slave_isr_cnt[SPI_SINLE_SLAVE_INT_LINE]){  //need to send and receive
      bcm2835_spi_transfernb(tx_buf, rx_buf, sizeof(tx_buf));
      slave_isr_cnt[SPI_SINLE_SLAVE_INT_LINE]--;
      recv_from = sid;
    }else if(sid){                                       //just to send
        bcm2835_spi_writenb(tx_buf, sizeof(tx_buf));
    }else{                                               //just to receive according to the isr event and count
      if( 0 && slave_isr_cnt[SPI_SINLE_SLAVE_INT_LINE]){
        memset(tx_buf,0xff,sizeof(tx_buf));
        bcm2835_spi_transfernb(tx_buf, rx_buf, sizeof(tx_buf));
        slave_isr_cnt[SPI_SINLE_SLAVE_INT_LINE]--;
        recv_from = SPI_SINLE_SLAVE_INT_LINE;
      }
    }
  #endif //SPI_MULTI_SLAVE_INT
    if(recv_from){
      spi_rx_data_in(recv_from,(uint8_t*)rx_buf,sizeof(rx_buf));
    }
     if(0)
     {
        int i = 0;
        printf("Master write %02x Read from Slave",tx_buf[0]);
        for(;i < sizeof(rx_buf);i++)
          printf("%02X ",rx_buf[i]);
        printf("\n");
        memset(rx_buf,0,sizeof(rx_buf));
      }
#else
    memcpy(rx_buf, tx_buf,sizeof(tx_buf));
    recv_from = sid;
    spi_rx_data_in(recv_from,(uint8_t*)rx_buf,sizeof(rx_buf));
#endif //SPI_TXRX_LOOP_TEST

    //usleep(1000000);
  }
 
  bcm2835_spi_end();
  bcm2835_close();
  return 0;
}


void spi_slave1_isr_handle(void)
{
#if defined SPI_MULTI_SLAVE_INT
  printf("spi slave 1 data ready!\n");
#else
  printf("spi slave data ready!");
#endif //SPI_MULTI_SLAVE_INT
  slave_isr_cnt[1]++;
  fflush (stdout) ;
}

#if defined SPI_MULTI_SLAVE_INT
void spi_slave2_isr_handle(void)
{
   printf("spi slave 2 data ready!\n");
   slave_isr_cnt[2]++;
   fflush (stdout) ;
}
void spi_slave3_isr_handle(void)
{
   printf("spi slave 3 data ready!\n");
   slave_isr_cnt[3]++;
   fflush (stdout) ;
}
void spi_slave4_isr_handle(void)
{
   printf("spi slave 4 data ready!\n");
   slave_isr_cnt[4]++;
   fflush (stdout) ;
}

#endif //SPI_MULTI_SLAVE_INT

void spi_end_handle(int param)
{
  printf("Spi end and free all list....\n");
  free_all_list();
  if(spi_init_done){
    bcm2835_spi_end();
    bcm2835_close();
  }
}
