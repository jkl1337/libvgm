/***********************************************************/
/*                                                         */
/* PCM.C : PCM RF5C164 emulator                            */
/*                                                         */
/* This source is a part of Gens project                   */
/* Written by St�phane Dallongeville (gens@consolemul.com) */
/* Copyright (c) 2002 by St�phane Dallongeville            */
/*                                                         */
/***********************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <stdtype.h>
#include "../snddef.h"
#include "scd_pcm.h"

struct pcm_chan_
{
	UINT8 ENV;				/* envelope register */
	UINT8 PAN;				/* pan register */
	UINT16 MUL_L;			/* envelope & pan product left */
	UINT16 MUL_R;			/* envelope & pan product right */
	UINT16 St_Addr;			/* start address register */
	UINT16 Loop_Addr;		/* loop address register */
	UINT32 Addr;			/* current address register */
	UINT32 Step;			/* frequency register */
	UINT16 Step_B;			/* frequency register binaire */
	UINT8 Enable;			/* channel on/off register */
	INT8 Data;				/* wave data */
	UINT8 Muted;
};
struct pcm_chip_
{
	void* chipInf;

	float Rate;
	UINT8 Smpl0Patch;
	UINT8 Enable;
	UINT8 Cur_Chan;
	UINT16 Bank;

	struct pcm_chan_ Channel[8];

	UINT32 RAMSize;
	UINT8* RAM;
};

#define PCM_STEP_SHIFT 11


/**
 * SCD_PCM_Init(): Initialize the PCM chip.
 * @param Rate Sample rate.
 * @return 0 if successful.
 */
void* SCD_PCM_Init(UINT32 Clock, UINT32 Rate, UINT8 smpl0patch)
{
	struct pcm_chip_ *chip;
	
	chip = (struct pcm_chip_ *)calloc(1, sizeof(struct pcm_chip_));
	if (chip == NULL)
		return NULL;
	
	chip->Smpl0Patch = 0;
	SCD_PCM_SetMuteMask(chip, 0x00);
	
	chip->RAMSize = 64 * 1024;
	chip->RAM = (UINT8*)malloc(chip->RAMSize);
	//SCD_PCM_Reset(chip);
	SCD_PCM_Set_Rate(chip, Clock, Rate);
	
	return chip;
}

void SCD_PCM_Deinit(void* info)
{
	struct pcm_chip_ *chip = (struct pcm_chip_ *)info;
	free(chip->RAM);	chip->RAM = NULL;
	free(chip);
	
	return;
}


/**
 * SCD_PCM_Reset(): Reset the PCM chip.
 */
void SCD_PCM_Reset(void* info)
{
	struct pcm_chip_ *chip = (struct pcm_chip_ *)info;
	int i;
	struct pcm_chan_* chan;
	
	// Clear the PCM memory.
	memset(chip->RAM, 0x00, chip->RAMSize);
	
	chip->Enable = 0;
	chip->Cur_Chan = 0;
	chip->Bank = 0;
	
	/* clear channel registers */
	for (i = 0; i < 8; i++)
	{
		chan = &chip->Channel[i];
		chan->Enable = 0;
		chan->ENV = 0;
		chan->PAN = 0;
		chan->St_Addr = 0;
		chan->Addr = 0;
		chan->Loop_Addr = 0;
		chan->Step = 0;
		chan->Step_B = 0;
		chan->Data = 0;
	}
}


/**
 * SCD_PCM_Set_Rate(): Change the PCM sample rate.
 * @param Rate New sample rate.
 */
void SCD_PCM_Set_Rate(void* info, UINT32 Clock, UINT32 Rate)
{
	struct pcm_chip_ *chip = (struct pcm_chip_ *)info;
	int i;
	
	if (Rate == 0)
		return;
	
	//chip->Rate = (float) (31.8f * 1024) / (float) Rate;
	chip->Rate = ((float)Clock / 384.0f) / (float)Rate;
	
	for (i = 0; i < 8; i++)
	{
		chip->Channel[i].Step =
			(int) ((float) chip->Channel[i].Step_B * chip->Rate);
	}
}


/**
 * SCD_PCM_Write_Reg(): Write to a PCM register.
 * @param Reg Register ID.
 * @param Data Data to write.
 */
void SCD_PCM_Write_Reg(void* info, UINT8 Reg, UINT8 Data)
{
	struct pcm_chip_ *chip = (struct pcm_chip_ *)info;
	int i;
	struct pcm_chan_* chan = &chip->Channel[chip->Cur_Chan];
	
	switch (Reg)
	{
		case 0x00:
			/* evelope register */
			chan->ENV = Data;
			chan->MUL_L = (Data * (chan->PAN & 0x0F)) >> 5;
			chan->MUL_R = (Data * (chan->PAN >> 4)) >> 5;
		break;
		
		case 0x01:
			/* pan register */
			chan->PAN = Data;
			chan->MUL_L = ((Data & 0x0F) * chan->ENV) >> 5;
			chan->MUL_R = ((Data >> 4) * chan->ENV) >> 5;
			break;
		
		case 0x02:
			/* frequency step (LB) registers */
			chan->Step_B &= 0xFF00;
			chan->Step_B |= Data;
			chan->Step = (UINT32)((float)chan->Step_B * chip->Rate);
			
			//LOG_MSG(pcm, LOG_MSG_LEVEL_DEBUG1,
			//	"Step low = %.2X   Step calculated = %.8X",
			//	Data, chan->Step);
			break;
		
		case 0x03:
			/* frequency step (HB) registers */
			chan->Step_B &= 0x00FF;
			chan->Step_B |= Data << 8;
			chan->Step = (UINT32)((float)chan->Step_B * chip->Rate);
			
			//LOG_MSG(pcm, LOG_MSG_LEVEL_DEBUG1,
			//	"Step high = %.2X   Step calculated = %.8X",
			//	Data, chan->Step);
			break;
		
		case 0x04:
			chan->Loop_Addr &= 0xFF00;
			chan->Loop_Addr |= Data;
			
			//LOG_MSG(pcm, LOG_MSG_LEVEL_DEBUG1,
			//	"Loop low = %.2X   Loop = %.8X",
			//	Data, chan->Loop_Addr);
			break;
		
		case 0x05:
			/* loop address registers */
			chan->Loop_Addr &= 0x00FF;
			chan->Loop_Addr |= Data << 8;
			
			//LOG_MSG(pcm, LOG_MSG_LEVEL_DEBUG1,
			//	"Loop high = %.2X   Loop = %.8X",
			//	Data, chan->Loop_Addr);
			break;
		
		case 0x06:
			/* start address registers */
			chan->St_Addr = Data << 8;
			
			//LOG_MSG(pcm, LOG_MSG_LEVEL_DEBUG1,
			//	"Start addr = %.2X   New Addr = %.8X",
			//	Data, chan->Addr);
			break;
		
		case 0x07:
			/* control register */
			/* mod is H */
			if (Data & 0x40)
			{
				/* select channel */
				chip->Cur_Chan = Data & 0x07;
			}
			/* mod is L */
			else
			{
				/* pcm ram bank select */
				chip->Bank = (Data & 0x0F) << 12;
			}
			
			/* sounding bit */
			if (Data & 0x80)
				chip->Enable = 0xFF;	// Used as mask
			else
				chip->Enable = 0;
			
			//LOG_MSG(pcm, LOG_MSG_LEVEL_DEBUG1,
			//	"General Enable = %.2X", Data);
			break;
		
		case 0x08:
			/* sound on/off register */
			Data = ~Data;
			
			//LOG_MSG(pcm, LOG_MSG_LEVEL_DEBUG1,
			//	"Channel Enable = %.2X", Data);
			
			for (i = 0; i < 8; i++)
			{
				chan = &chip->Channel[i];
				if (!chan->Enable)
					chan->Addr = chan->St_Addr << PCM_STEP_SHIFT;
			}
			
			for (i = 0; i < 8; i++)
			{
				chip->Channel[i].Enable = Data & (1 << i);
			}
	}
}

UINT8 SCD_PCM_Read_Reg(void* info, UINT8 Reg)
{
	struct pcm_chip_ *chip = (struct pcm_chip_ *)info;
	UINT8 shift;
	UINT8 chn;
	
	shift = (Reg & 0x01) ? 8 : 0;
	chn = (Reg >> 1) & 0x07;
	return chip->Channel[chn].Addr >> (shift + PCM_STEP_SHIFT);
}

/**
 * SCD_PCM_Update(): Update the PCM buffer.
 * @param Length Buffer length.
 * @param buf PCM buffer.
 */
void SCD_PCM_Update(void* info, UINT32 Length, DEV_SMPL **buf)
{
	struct pcm_chip_ *chip = (struct pcm_chip_ *)info;
	int i;
	UINT32 j;
	DEV_SMPL *bufL, *bufR;
	UINT32 Addr, k;
	struct pcm_chan_ *CH;
	
	bufL = buf[0];
	bufR = buf[1];
	
	// clear buffers
	memset(bufL, 0, Length * sizeof(DEV_SMPL));
	memset(bufR, 0, Length * sizeof(DEV_SMPL));
	
	// if PCM disable, no sound
	if (!chip->Enable)
		return;
	
	// for long update
	for (i = 0; i < 8; i++)
	{
		CH = &(chip->Channel[i]);
		
		// only loop when sounding and on
		if (CH->Enable && ! CH->Muted)
		{
			Addr = CH->Addr >> PCM_STEP_SHIFT;
			
			for (j = 0; j < Length; j++)
			{
				// test for loop signal
				if (chip->RAM[Addr] == 0xFF)
				{
					CH->Addr = (Addr = CH->Loop_Addr) << PCM_STEP_SHIFT;
					if (chip->RAM[Addr] == 0xFF)
						break;
					else
						j--;
				}
				else
				{
					if (chip->RAM[Addr] & 0x80)
					{
						CH->Data = chip->RAM[Addr] & 0x7F;
						bufL[j] -= CH->Data * CH->MUL_L;
						bufR[j] -= CH->Data * CH->MUL_R;
					}
					else
					{
						CH->Data = chip->RAM[Addr];
						// This improves the sound of Cosmic Fantasy Stories,
						// although it's definitely false behaviour.
						if (! CH->Data && chip->Smpl0Patch)
							CH->Data = -0x7F;
						bufL[j] += CH->Data * CH->MUL_L;
						bufR[j] += CH->Data * CH->MUL_R;
					}
					
					// update address register
					k = Addr + 1;
					CH->Addr = (CH->Addr + CH->Step) & 0x7FFFFFF;
					Addr = CH->Addr >> PCM_STEP_SHIFT;
					
					for (; k < Addr; k++)
					{
						if (chip->RAM[k] == 0xFF)
						{
							CH->Addr = (Addr = CH->Loop_Addr) << PCM_STEP_SHIFT;
							break;
						}
					}
				}
			}
			
			if (chip->RAM[Addr] == 0xFF)
			{
				CH->Addr = CH->Loop_Addr << PCM_STEP_SHIFT;
			}
		}
	}
	
	return;
}


UINT8 SCD_PCM_MemRead(void *info, UINT16 offset)
{
	struct pcm_chip_ *chip = (struct pcm_chip_ *)info;
	return chip->RAM[chip->Bank | offset];
}

void SCD_PCM_MemWrite(void *info, UINT16 offset, UINT8 data)
{
	struct pcm_chip_ *chip = (struct pcm_chip_ *)info;
	chip->RAM[chip->Bank | offset] = data;
}

void SCD_PCM_MemBlockWrite(void* info, UINT32 offset, UINT32 length, const UINT8* data)
{
	struct pcm_chip_ *chip = (struct pcm_chip_ *)info;
	
	//offset |= chip->Bank;
	if (offset >= chip->RAMSize)
		return;
	if (offset + length > chip->RAMSize)
		length = chip->RAMSize - offset;
	
	memcpy(chip->RAM + offset, data, length);
	
	return;
}


void SCD_PCM_SetMuteMask(void* info, UINT32 MuteMask)
{
	struct pcm_chip_ *chip = (struct pcm_chip_ *)info;
	unsigned char CurChn;
	
	for (CurChn = 0; CurChn < 8; CurChn ++)
		chip->Channel[CurChn].Muted = (MuteMask >> CurChn) & 0x01;
	
	return;
}