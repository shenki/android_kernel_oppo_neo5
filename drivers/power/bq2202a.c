/************************************************************************************
** File:  bq2202a.c
**
** Copyright (C), 2008-2012, OPPO Mobile Comm Corp., Ltd
**
** Description:
**      for battery encryption  bq2202a
**
** Version: 1.0
** Date created: 22:45:46,04/09/2014
** Author: Fanhong.Kong@ProDrv.CHG
**
** --------------------------- Revision History: ------------------------------------------------------------
** 	<author>	<data>			<desc>
** 1.1          2014-4-19       Fanhong.Kong@ProDrv.CHG             qcom bq2202a ic
************************************************************************************************************/

#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/qpnp/qpnp-adc.h>
#include <mach/oppo_boot_mode.h>



extern bool oppo_high_battery_status;
extern int oppo_check_ID_status;

#define DEBUG_BQ2202A
#define READ_PAGE_BQ2202A

#define BQ2202A_GPIO 				53
#define GPIO_DIR_OUT_1				1
#define GPIO_DIR_OUT_0				0



#define READ_ID_CMD                 0x33            // read ROM
#define SKIP_ROM_CMD               0xCC           // skip ROM
#define WRITE_EPROM_CMD        0x0F           // write EPROM
#define READ_PAGE_ID_CMD        0xF0          // read EPROM  PAGE
#define READ_FIELD_ID_CMD       0xC3          // read EPROM  FIELD

#ifdef READ_PAGE_BQ2202A
#define AddressLow                     0x20        // EPROM start address LOW
#define AddressHigh                     0x00        // EPROM start address  HIGH
#else
#define AddressLow                     0x00        // EPROM start address LOW
#define AddressHigh                     0x00        // EPROM start address  HIGH
#endif

static  unsigned char ReadIDDataByte[8];     //8*8=64bit            ID ROM
#ifdef READ_PAGE_BQ2202A
static  unsigned char CheckIDDataByte[32];    // 32*8=256bit   EPROM  PAGE1
#else
static  unsigned char CheckIDDataByte[128];    // 128*8=1024bit   EPROM  PAGE1
#endif

static DEFINE_MUTEX(bq2202a_access);
/**********************************************************************/
/* 		void wait_us(int usec)										  */
/*																      */
/*	Description :   Creates a delay of approximately (usec + 5us) 	  */
/*				  		when usec is greater.						  */
/* 	Arguments : 		None										  */
/*	Global Variables:	None   										  */
/*  Returns: 			None								          */
/**********************************************************************/
#define wait_us(n) udelay(n)
#define wait_ms(n) mdelay(n)

int Gpio_BatId_Init(void)
{
	int rc = 0;
	if(gpio_is_valid(BQ2202A_GPIO))
	{
		rc = gpio_request(BQ2202A_GPIO,"batid_bq2202a");
		//pr_err("Gpio_BatId_Init,gpio_request batid_bq2202a\r\n");
		if(rc)
		{
			pr_err("unable to request gpio batid_bq2202a\r\n");
			return rc;
		}
	}
	else
	{
		pr_err("Gpio_BatId_Init,BQ2202A_GPIO is not valid\r\n");
		rc = -1;
	}
	return rc;
}
/**********************************************************************/
/* 	static void SendReset(void)										  */
/*																      */
/*	Description : 		Creates the Reset signal to initiate SDQ 	  */
/*				  		communication.								  */
/* 	Arguments : 		None										  */
/*	Global Variables:	None   										  */
/*  Returns: 			None								          */
/**********************************************************************/
static void SendReset(void)
{

    gpio_direction_output(BQ2202A_GPIO, GPIO_DIR_OUT_1);	            //Set High
    wait_us(20);													//Allow PWR cap to charge and power IC	~ 25us
    gpio_direction_output(BQ2202A_GPIO, GPIO_DIR_OUT_0);           	//Set Low
    wait_us(650);								//Reset time greater then 480us
    //mt_set_gpio_out(BQ2202A_GPIO, 1);            	//Set High
    gpio_direction_input(BQ2202A_GPIO);		//Set GPIO P9.3 as Input
}

/**********************************************************************/
/* 	static unsigned char TestPresence(void)							  */
/*																      */
/*	Description : 		Detects if a device responds to Reset signal  */
/* 	Arguments : 		PresenceTimer - Sets timeout if no device	  */
/*							present									  */
/*						InputData - Actual state of GPIO			  */
/*						GotPulse - States if a pulse was detected	  */
/*	Global Variables:	None   										  */
/*  Returns: 			GotPulse         							  */
/**********************************************************************/
static unsigned char TestPresence(void)
{
    unsigned int PresenceTimer;
    static volatile unsigned char InputData;
    static volatile unsigned char GotPulse;

    gpio_direction_input(BQ2202A_GPIO);	        //Set GPIO P9.3 as Input
    PresenceTimer = 300;                                                //Set timeout, enough time to allow presence pulse
    GotPulse = 0;                                                           //Initialize as no pulse detected
	wait_us(60);
    while ((PresenceTimer > 0) && (GotPulse == 0))
    {
        InputData = gpio_get_value(BQ2202A_GPIO);       //Monitor logic state of GPIO
		/*int j = 0;
		while(j < 10)
		{
			printk("mt_get_gpio_in---------------InputData = %d\r\n", InputData);
			j++;
			wait_us(100);

		}*/
        if (InputData == 0)                                             //If GPIO is Low,
        {
            GotPulse = 1;                                               //it means that device responded
        }
        else                                                                //If GPIO is high
        {
            GotPulse = 0;			                            //it means that device has not responded
            --PresenceTimer;		                            //Decrease timeout until enough time has been allowed for response
        }
    }
    wait_us(200);					                    //Wait some time to continue SDQ communication
    return GotPulse;				                    //Return if device detected or not
}

/**********************************************************************/
/* 	static void WriteOneBit(unsigned char OneZero)					  */
/*																      */
/*	Description : 		This procedure outputs a bit in SDQ to the 	  */
/*				  		slave.								  		  */
/* 	Arguments : 		OneZero - value of bit to be written		  */
/*	Global Variables:	None   										  */
/*  Returns: 			None								          */
/**********************************************************************/
static void WriteOneBit(unsigned char OneZero)
{
	//wait_us(300);
	gpio_direction_output(BQ2202A_GPIO, GPIO_DIR_OUT_1);			//Set GPIO P9.3 as Output
    //mt_set_gpio_out(BQ2202A_GPIO, 1);		            //Set High
     gpio_direction_output(BQ2202A_GPIO, GPIO_DIR_OUT_0);	                //Set Low

     //printk("WriteOneBit----1------OneZero = %d\t\n", OneZero);
    if (OneZero != 0x00)
    {
        wait_us(7);									//approximately 7us	for a Bit 1
        gpio_direction_output(BQ2202A_GPIO, GPIO_DIR_OUT_1);	            //Set High
        wait_us(65);								//approximately 65us
    }
    else
    {
        wait_us(65);								//approximately 65us for a Bit 0
        gpio_direction_output(BQ2202A_GPIO, GPIO_DIR_OUT_1);	            //Set High
        wait_us(7);					   				//approximately 7us
    }
    wait_us(5);	  									//approximately 5us
}

/**********************************************************************/
/* 	static void WriteOneByte(unsigned char Data2Send)				  */
/*																      */
/*	Description : 		This procedure calls the WriteOneBit() 		  */
/*				  		function 8 times to send a byte in SDQ.		  */
/* 	Arguments : 		Data2Send - Value of byte to be sent in SDQ	  */
/*	Global Variables:	None   										  */
/*  Returns: 			None								          */
/**********************************************************************/
static void WriteOneByte(unsigned char Data2Send)
{
    unsigned char i;
    unsigned char MaskByte;
    unsigned char Bit2Send;

    MaskByte = 0x01;

    for (i = 0; i < 8; i++)
    {
        Bit2Send = Data2Send & MaskByte;		//Selects the bit to be sent
        WriteOneBit(Bit2Send);					//Writes the selected bit
        MaskByte <<= 1;							//Moves the bit mask to the next most significant position
    }
}

/**********************************************************************/
/* 	static unsigned char ReadOneBit(void)							  */
/*																      */
/*	Description : 		This procedure receives the bit value returned*/
/*				  		by the SDQ slave.							  */
/* 	Arguments : 		InBit - Bit value returned by slave			  */
/*	Global Variables:	None   										  */
/*  Returns: 			InBit								          */
/**********************************************************************/
static unsigned char ReadOneBit(void)
{
    static unsigned char InBit;

    gpio_direction_output(BQ2202A_GPIO, GPIO_DIR_OUT_1);			//Set GPIO P9.3 as Output
    													            //Set High
    gpio_direction_output(BQ2202A_GPIO, GPIO_DIR_OUT_0);	                //Set Low
   	gpio_direction_input(BQ2202A_GPIO);			//Set GPIO P9.3 as Input
    wait_us(15);		   								//Strobe window	~ 12us
    InBit = gpio_get_value(BQ2202A_GPIO);		        //This function takes about 3us
													//Between the wait_us and GPIO_ReadBit functions
													//approximately 15us should occur to monitor the
													//GPIO line and determine if bit read is one or zero
    wait_us(65);									//End of Bit
    gpio_direction_output(BQ2202A_GPIO, GPIO_DIR_OUT_1);			//Set GPIO P9.3 as Output
    													            //Set High
    return InBit;									//Return bit value
}

/**********************************************************************/
/* 	static unsigned char ReadOneByte(void)							  */
/*																      */
/*	Description : 		This procedure reads 8 bits on the SDQ line   */
/*				  		and returns the byte value.					  */
/* 	Arguments : 		Databyte - Byte value returned by SDQ slave	  */
/*						MaskByte - Used to seperate each bit	      */
/*						i - Used for 8 time loop					  */
/*	Global Variables:	None   										  */
/*  Returns: 			DataByte							          */
/**********************************************************************/
static unsigned char ReadOneByte(void)
{
    unsigned char i;
    unsigned char DataByte;
    unsigned char MaskByte;

    DataByte = 0x00;			 				//Initialize return value

    for (i = 0; i < 8; i++)                                      //Select one bit at a time
    {
        MaskByte = ReadOneBit();				    //Read One Bit
        MaskByte <<= i;							//Determine Bit position within the byte
        DataByte = DataByte | MaskByte;			//Keep adding bits to form the byte
    }
    return DataByte;							//Return byte value read
}

/**********************************************************************/
/* 	void ReadBq2202aID(void)               							  */
/*																      */
/*	Description : 		This procedure reads BQ2202A'S ID on the SDQ  */
/*				  		line.                   					  */
/* 	Arguments : 		None                    					  */
/*	Global Variables:	None   										  */
/*  Returns: 			None       							          */
/**********************************************************************/
void ReadBq2202aID(void)
{
    unsigned char i;
    mutex_lock(&bq2202a_access);

    SendReset();
    wait_us(2);
    i=TestPresence();
    #ifdef DEBUG_BQ2202A
    //printk("TestPresence=%d\n",i);
    #endif
	//printk("TestPresence----1------WriteOneByte\t\n");
    //wait_us(600);
	//printk("TestPresence----2------WriteOneByte\t\n");
    WriteOneByte(READ_ID_CMD);                      // read rom commond
    for(i=0;i<8;i++)
    {
        //ReadIDDataByte[i] = ReadOneByte();      // read rom Partition 64bits = 8Bits
        ReadIDDataByte[7-i] = ReadOneByte();      // read rom Partition 64bits = 8Bits
    }

    mutex_unlock(&bq2202a_access);
	printk("ReadBq2202aID[0]  =%03d,ReadBq2202aID[1]  =%03d,ReadBq2202aID[2]  =%03d,ReadBq2202aID[3]  =%03d,ReadBq2202aID[4]  =%03d,ReadBq2202aID[5]  =%03d,ReadBq2202aID[6]  =%03d,ReadBq2202aID[7]  =%03d\n",ReadIDDataByte[0],ReadIDDataByte[1],ReadIDDataByte[2],ReadIDDataByte[3],ReadIDDataByte[4],ReadIDDataByte[5],ReadIDDataByte[6],ReadIDDataByte[7]);
}
/**********************************************************************/
/* 	void CheckBq2202aID(void)               							  */
/*																      */
/*	Description : 		This procedure reads BQ2202A'S ID on the SDQ  */
/*				  		line.                   					  */
/* 	Arguments : 		None                    					  */
/*	Global Variables:	None   										  */
/*  Returns: 			None       							          */
/**********************************************************************/
void CheckBq2202aID(void)
{
    unsigned char i;
    mutex_lock(&bq2202a_access);

    SendReset();
    wait_us(2);
    i=TestPresence();
    WriteOneByte(SKIP_ROM_CMD);              // skip rom commond
    wait_us(60);

#ifdef READ_PAGE_BQ2202A
    WriteOneByte(READ_PAGE_ID_CMD);     // read eprom Partition for page mode
#else
    WriteOneByte(READ_FIELD_ID_CMD);     // read eprom Partition for field mode
#endif
    wait_us(60);
    WriteOneByte(AddressLow);               // read eprom Partition Starting address low
    wait_us(60);
    WriteOneByte(AddressHigh);               // read eprom Partition Starting address high

#ifdef READ_PAGE_BQ2202A
    for(i=0;i<32;i++)
    {
        CheckIDDataByte[i] = ReadOneByte();   // read eprom Partition page1  256bits = 32Bits
    }

    mutex_unlock(&bq2202a_access);
	printk("CheckBq2202aID[16]=%03d,CheckBq2202aID[17]=%03d,CheckBq2202aID[18]=%03d,CheckBq2202aID[19]=%03d,CheckBq2202aID[20]=%03d,CheckBq2202aID[21]=%03d,CheckBq2202aID[22]=%03d,CheckBq2202aID[23]=%03d\n",CheckIDDataByte[16],CheckIDDataByte[17],CheckIDDataByte[18],CheckIDDataByte[19],CheckIDDataByte[20],CheckIDDataByte[21],CheckIDDataByte[22],CheckIDDataByte[23]);

#else
    for(i=0;i<128;i++)
    {
        CheckIDDataByte[i] = ReadOneByte();   // read eprom Partition field  1024bits = 128Bits
    }
    mutex_unlock(&bq2202a_access);

    #ifdef DEBUG_BQ2202A
    for(i=0;i<128;i++)
    {
        printk("CheckBq2202aID[%d]=%d\n",i,CheckIDDataByte[i]);
    }
    #endif
#endif
}
/**********************************************************************/
/* 	void CheckIDCompare(void)               							  */
/*																      */
/*	Description : 		This procedure reads BQ2202A'S ID on the SDQ  */
/*				  		line.                   					  */
/* 	Arguments : 		None                    					  */
/*	Global Variables:	None   										  */
/*  Returns: 			None       							          */
/**********************************************************************/

void CheckIDCompare(void)
{
    unsigned char i,j;
    int IDReadSign=1;

    if(IDReadSign==1)
    {
        for(i=0;i<1;i++)
        {
            ReadBq2202aID();
            CheckBq2202aID();

            oppo_check_ID_status=0;
            if(ReadIDDataByte[7] == 0x09)
            {
                for(j=1;j<7;j++)
                {
                    if((ReadIDDataByte[j] ==CheckIDDataByte[j+16]) && (ReadIDDataByte[j]!=0xff)  && (ReadIDDataByte[j] != 0))
                    {
                        oppo_check_ID_status++;
                    }
                }
                if(oppo_check_ID_status > 0)
                {
                    IDReadSign=0;
                    return;
                }
            }
            else
            {
                continue;
            }
        }
        IDReadSign=0;
    }
}
