// GB Enhanced Copyright Daniel Baxter 2013
// Licensed under the GPLv2
// See LICENSE.txt for full license text

// File : apu.cpp
// Date : November 18, 2013
// Description : Game Boy APU emulation
//
// Sets up SDL audio for mixing
// Generates and mixes samples for the GB's 4 sound channels

#include <iostream>
#include <cmath>

#include "apu.h"

/****** APU Constructor ******/
APU::APU()
{

	mem_link = NULL;

	//Reset voices
	for(int x = 0; x < 4; x++)
	{
		channel[x].freq_dist = 0;
		channel[x].sample_length = 0;
		channel[x].frequency = 0;
		channel[x].duration = 0;
		channel[x].playing = false;
		channel[x].volume = 0;

		channel[x].duty_cycle_start = 1;
		channel[x].duty_cycle_end = 5;

		channel[x].envelope_direction = 2;
		channel[x].envelope_step = 0;
		channel[x].envelope_counter = 0;

		channel[x].sweep_direction = 2;
		channel[x].sweep_step = 0;
		channel[x].sweep_counter = 0;
		channel[x].sweep_time = 0;

		channel[x].wave_step = 0;
		channel[x].wave_shift = 0;
	}

	//Initialize SDL audio
	setup = false;

        SDL_InitSubSystem(SDL_INIT_AUDIO);

    	desired_spec.freq = 44100;
	desired_spec.format = AUDIO_S16SYS;
    	desired_spec.channels = 1;
    	desired_spec.samples = 2048;
    	desired_spec.callback = audio_callback;
    	desired_spec.userdata = this;

    	//Open SDL audio for desired specification
	if(SDL_OpenAudio(&desired_spec, &obtained_spec) < 0) { std::cout<<"APU : Failed to open audio\n"; }
	else if(desired_spec.format != obtained_spec.format) 
	{ 
		std::cout<<"APU : Could not obtain desired audio format\n";
		SDL_CloseAudio();
	}

	else
	{ 
		setup = true;
	}
}

/****** APU Deconstructor ******/
APU::~APU() { }

/****** Update GB sound channel 1 ******/
void APU::update_channel_1(u16 update_addr)
{
	switch(update_addr)
	{
		//Volume & Envelope
		case 0xFF12:
			{
				u8 current_step = channel[0].envelope_step;
				u8 next_step = (mem_link->memory_map[0xFF12] & 0x07) ? 1 : 0;

				//Envelope timer is not reset unless sound is initializes
				//Envelope timer does start if it is turned off at first, but turned on after sound initializes
				if((current_step == 0) && (next_step != 0)) 
				{
					channel[0].volume = (mem_link->memory_map[0xFF12] >> 4);
					channel[0].envelope_direction = (mem_link->memory_map[0xFF12] & 0x08) ? 1 : 0;
					channel[0].envelope_step = (mem_link->memory_map[0xFF12] & 0x07);
					channel[0].envelope_counter = 0; 
				}
			}
			break;

		//Frequency -  Low 8-bits
		case 0xFF13:
			//Frequency will not change when sweep function is active
			if(channel[0].sweep_time != 0)
			{
				channel[0].raw_frequency &= 0x700;
				channel[0].raw_frequency |= mem_link->memory_map[0xFF13];
				channel[0].frequency = 4194304.0/(32 * (2048-channel[0].raw_frequency));
			}
			break;
	}
}			

/****** Play GB sound channel 1 - Square wave generator 1 ******/
void APU::play_channel_1()
{
	//Play sound only if Channel 1's Status is ON and Trigger bit is set
	if(((mem_link->memory_map[0xFF25] & 0x1) || (mem_link->memory_map[0xFF25] & 0x10)) && (mem_link->memory_map[mem_link->apu_update_addr] & 0x80))
	{
		channel[0].freq_dist = 0;
		channel[0].frequency = 0;
		channel[0].duration = 0;
		channel[0].playing = true;
		channel[0].envelope_counter = 0;
		channel[0].sweep_counter = 0;

		//Determine duty cycle in 8ths
		switch((mem_link->memory_map[0xFF11] >> 6))
		{
			case 0x0:
				channel[0].duty_cycle_start = 1;
				channel[0].duty_cycle_end = 2;
				break;

			case 0x1:
				channel[0].duty_cycle_start = 0;
				channel[0].duty_cycle_end = 2;
				break;

			case 0x2:
				channel[0].duty_cycle_start = 0;
				channel[0].duty_cycle_end = 4;
				break;

			case 0x3:
				channel[0].duty_cycle_start = 2;
				channel[0].duty_cycle_end = 8;
				break;
		}

		//Duration
		if((mem_link->memory_map[0xFF14] & 0x40) == 0) { channel[0].duration = 5000; }
		
		else 
		{
			channel[0].duration = (mem_link->memory_map[0xFF11] & 0x3F);
			channel[0].duration = 1000/(256/(64 - channel[0].duration));
		}

		channel[0].sample_length = (channel[0].duration * 44100)/1000;

		//Frequency
		channel[0].raw_frequency = (mem_link->memory_map[0xFF14] & 0x7);
		channel[0].raw_frequency <<= 8;
		channel[0].raw_frequency |= mem_link->memory_map[0xFF13];
		channel[0].raw_frequency = (channel[0].raw_frequency & 0x7FF);
		channel[0].frequency = 4194304.0/(32 * (2048-channel[0].raw_frequency));
			
		//Volume & Envelope
		channel[0].volume = (mem_link->memory_map[0xFF12] >> 4);
		channel[0].envelope_direction = (mem_link->memory_map[0xFF12] & 0x08) ? 1 : 0;
		channel[0].envelope_step = (mem_link->memory_map[0xFF12] & 0x07);

		//Sweep
		channel[0].sweep_direction = (mem_link->memory_map[0xFF10] & 0x08) ? 1 : 0;
		channel[0].sweep_time = ((mem_link->memory_map[0xFF10] >> 4) & 0x7);
		channel[0].sweep_step = (mem_link->memory_map[0xFF10] & 0x7);
	} 
}

/****** Play GB sound channel 2 - Square wave generator 2 ******/
void APU::play_channel_2()
{
	//Play sound only if Channel 2's Status is ON and Trigger bit is set
	if(((mem_link->memory_map[0xFF25] & 0x2) || (mem_link->memory_map[0xFF25] & 0x20)) && (mem_link->memory_map[mem_link->apu_update_addr] & 0x80))
	{
		channel[1].freq_dist = 0;
		channel[1].frequency = 0;
		channel[1].duration = 0;
		channel[1].playing = true;
		channel[1].envelope_counter = 0;

		//Determine duty cycle in 8ths
		switch((mem_link->memory_map[0xFF16] >> 6))
		{
			case 0x0:
				channel[1].duty_cycle_start = 1;
				channel[1].duty_cycle_end = 2;
				break;

			case 0x1:
				channel[1].duty_cycle_start = 0;
				channel[1].duty_cycle_end = 2;
				break;

			case 0x2:
				channel[1].duty_cycle_start = 0;
				channel[1].duty_cycle_end = 4;
				break;

			case 0x3:
				channel[1].duty_cycle_start = 2;
				channel[1].duty_cycle_end = 8;
				break;
		}

		//Duration
		if((mem_link->memory_map[0xFF19] & 0x40) == 0) { channel[1].duration = 5000; }
		
		else 
		{
			channel[1].duration = (mem_link->memory_map[0xFF16] & 0x3F);
			channel[1].duration = 1000/(256/(64 - channel[1].duration));
		}

		channel[1].sample_length = (channel[1].duration * 44100)/1000;

		//Frequency
		u32 frequency = mem_link->memory_map[0xFF19];
		frequency <<= 8;
		frequency |= mem_link->memory_map[0xFF18];
		frequency = (frequency & 0x7FF);
		channel[1].frequency = 4194304.0/(32 * (2048-frequency));
			
		//Volume & Envelope
		channel[1].volume = (mem_link->memory_map[0xFF17] >> 4);
		channel[1].envelope_direction = (mem_link->memory_map[0xFF17] & 0x08) ? 1 : 0;
		channel[1].envelope_step = (mem_link->memory_map[0xFF17] & 0x07);
	}
}

/****** Play GB sound channel 3 - RAM Waveform ******/
void APU::play_channel_3()
{
	//Play sound only if Channel 3's Status is ON and Trigger bit is set
	if(((mem_link->memory_map[0xFF25] & 0x4) || (mem_link->memory_map[0xFF25] & 0x40)) && (mem_link->memory_map[mem_link->apu_update_addr] & 0x80))
	{
		//Frequency, duration, wave RAM, etc, can dynamically changed
		//The rest of the info should be determined upon sample generation
		channel[2].freq_dist = 0;
		channel[2].frequency = 0;
		channel[2].duration = 0;
		channel[2].playing = true;
	}
}

/******* Generate samples for GB sound channel 1 ******/
void APU::generate_channel_1_samples(s16* stream, int length)
{
	//Process samples if playing
	if(channel[0].playing)
	{
		int freq_samples = 44100/channel[0].frequency;

		for(int x = 0; x < length; x++, channel[0].sample_length--)
		{
			if(channel[0].sample_length > 0)
			{
				channel[0].freq_dist++;

				//Process audio sweep
				if(channel[0].sweep_time >= 1)
				{
					channel[0].sweep_counter++;

					if(channel[0].sweep_counter % ((44100/128) * channel[0].sweep_time) == 0)
					{
						//Increase frequency
						if(channel[0].sweep_direction == 0)
						{
							double pre_calc;
							if(channel[0].sweep_step >= 1) { pre_calc = (channel[0].frequency/(2 << (channel[0].sweep_step - 1))); }
							else { pre_calc = (channel[0].frequency); }

							//When frequency is greater than 131KHz, stop sound - reset NR52 in future
							if((channel[0].frequency + pre_calc) >= 2048) 
							{ 
								channel[0].volume = channel[0].sweep_step = channel[0].envelope_step = channel[0].sweep_time = 0; 
								channel[0].playing = false; 
							}
	
							else { channel[0].frequency += pre_calc; }
						}

						//Decrease frequency
						else if(channel[0].sweep_direction == 1)
						{
							double pre_calc;
							if(channel[0].sweep_step >= 1) { pre_calc = (channel[0].frequency/(2 << (channel[0].sweep_step - 1))); }
							else { pre_calc = (channel[0].frequency); }

							//Only sweep down when result of frequency change is greater than zero
							if((channel[0].frequency - pre_calc) >= 0) { channel[0].frequency -= pre_calc; }
						}

						channel[0].sweep_counter = 0;
					}
				} 

				//Process audio envelope
				if(channel[0].envelope_step >= 1)
				{
					channel[0].envelope_counter++;

					if(channel[0].envelope_counter >= ((44100.0/64) * channel[0].envelope_step)) 
					{		
						//Decrease volume
						if((channel[0].envelope_direction == 0) && (channel[0].volume >= 1)) { channel[0].volume--; }
				
						//Increase volume
						else if((channel[0].envelope_direction == 1) && (channel[0].volume < 0xF)) { channel[0].volume++; }

						channel[0].envelope_counter = 0;
					}
				}

				//Reset frequency distance
				if(channel[0].freq_dist >= freq_samples) { channel[0].freq_dist = 0; }
		
				//Generate high wave form if duty cycle is on AND volume is not muted
				if((channel[0].freq_dist >= (freq_samples/8) * channel[0].duty_cycle_start) && (channel[0].freq_dist < (freq_samples/8) * channel[0].duty_cycle_end) && (channel[0].volume >= 1))
				{
					stream[x] = (32767/16) * channel[0].volume;
				}

				//Generate low wave form if duty cycle is off OR volume is muted
				else { stream[x] = -32768; }

			}

			//Continuously generate sound if necessary
			else if((channel[0].sample_length == 0) && (channel[0].duration == 5000)) { channel[0].sample_length = (channel[0].duration * 44100)/1000; }

			//Or stop sound after duration has been met
			else { channel[0].sample_length = 0; stream[x] = -32768; channel[0].playing = false; }
		}
	}

	//Otherwise, generate silence
	else 
	{
		for(int x = 0; x < length; x++) { stream[x] = -32768; }
	}	
}

/******* Generate samples for GB sound channel 2 ******/
void APU::generate_channel_2_samples(s16* stream, int length)
{
	//Process samples if playing
	if(channel[1].playing)
	{
		int freq_samples = 44100/channel[1].frequency;

		for(int x = 0; x < length; x++, channel[1].sample_length--)
		{
			if(channel[1].sample_length > 0)
			{
				channel[1].freq_dist++;
				channel[1].envelope_counter++;

				//Process audio envelope
				if(channel[1].envelope_step >= 1)
				{
					if(channel[1].envelope_counter % ((44100/64) * channel[1].envelope_step) == 0) 
					{		
						//Decrease volume
						if((channel[1].envelope_direction == 0) && (channel[0].volume >= 1)) { channel[1].volume--; }
				
						//Increase volume
						else if((channel[1].envelope_direction == 1) && (channel[1].volume < 0xF)) { channel[1].volume++; }

						channel[1].envelope_counter = 0;
					}
				}

				//Reset frequency distance
				if(channel[1].freq_dist >= freq_samples) { channel[1].freq_dist = 0; }
		
				//Generate high wave form if duty cycle is on AND volume is not muted
				if((channel[1].freq_dist >= (freq_samples/8) * channel[1].duty_cycle_start) && (channel[1].freq_dist < (freq_samples/8) * channel[1].duty_cycle_end) && (channel[1].volume >= 1))
				{
					stream[x] = (32767/16) * channel[1].volume;
				}

				//Generate low wave form if duty cycle is off OR volume is muted
				else { stream[x] = -32768; }

			}

			//Continuously generate sound if necessary
			else if((channel[1].sample_length == 0) && (channel[1].duration == 5000)) { channel[1].sample_length = (channel[1].duration * 44100)/1000; }

			//Or stop sound after duration has been met
			else { channel[1].sample_length = 0; stream[x] = -32768; channel[1].playing = false; }
		}
	}

	//Otherwise, generate silence
	else 
	{
		for(int x = 0; x < length; x++) { stream[x] = -32768; }
	}	
}

/******* Generate samples for GB sound channel 3 ******/
void APU::generate_channel_3_samples(s16* stream, int length)
{
	//Process samples if playing
	if(channel[2].playing)
	{
		//Duration
		if((mem_link->memory_map[0xFF1E] & 0x40) == 0) 
		{
			if(channel[2].duration != 5000)
			{
				channel[2].duration = 5000; 
				channel[2].sample_length = (channel[2].duration * 44100)/1000; 
			}
		}
		
		else 
		{
			u32 temp_duration = mem_link->memory_map[0xFF1B];
			temp_duration = (1000/256) * (256 - temp_duration);
			
			if(temp_duration != channel[2].duration) 
			{ 
				channel[2].duration = temp_duration;
				channel[2].sample_length = (channel[2].duration * 44100)/1000; 
			}
		}

		//Frequency
		u32 frequency = mem_link->memory_map[0xFF1E];
		frequency <<= 8;
		frequency |= mem_link->memory_map[0xFF1D];
		frequency = (frequency & 0x7FF);
		channel[2].frequency = 4194304.0/(32 * (2048-frequency));

		//Sound channel 3's frequency timer period runs twice as long as, say sound channel 1
		//Since GBE does not have cycle-accurate APU emulation, it halves the frequency here
		//Output should still be identical, sounds right in homebrew tests
		//It works, trust me
		channel[2].frequency /= 2;

		//Determine waveform RAM shifting
		switch(((mem_link->memory_map[0xFF1C] >> 5) & 0x3))
		{
			case 0x0:
				channel[2].wave_shift = 4;
				break;
	
			case 0x1:
				channel[2].wave_shift = 0;
				break;
		
			case 0x2:
				channel[2].wave_shift = 1;
				break;

			case 0x3:
				channel[2].wave_shift = 2;
				break;
		}

		//Determine amount of samples per waveform step
		channel[2].wave_step = (44100.0/channel[2].frequency)/32.0;

		int freq_samples = 44100/channel[2].frequency;

		for(int x = 0; x < length; x++, channel[2].sample_length--)
		{
			if(channel[2].sample_length > 0)
			{
				channel[2].freq_dist++;
				
				//Reset frequency distance
				if(channel[2].freq_dist >= freq_samples) { channel[2].freq_dist = 0; }

				//Determine which step in the waveform the current sample corresponds to
				int ram_count = int(floor(channel[2].freq_dist/channel[2].wave_step)) % 32;

				//Grab wave RAM step data for even steps
				if(ram_count % 2 == 0)
				{
					u32 address = 0xFF30 + (ram_count/2);
					u8 wave_ram_data = (mem_link->memory_map[address] >> 4) >> channel[2].wave_shift;
	
					//Scale waveform to S16 audio stream
					stream[x] = -32768 + (4369 * wave_ram_data);
				}

				//Grab wave RAM step data for odd steps
				else
				{
					u32 address = 0xFF30 + (ram_count/2);
					u8 wave_ram_data = (mem_link->memory_map[address] & 0xF) >> channel[2].wave_shift;
	
					//Scale waveform to S16 audio stream
					stream[x] = -32768 + (4369 * wave_ram_data);
				}
			}

			//Continuously generate sound if necessary
			else if((channel[2].sample_length == 0) && (channel[2].duration == 5000)) { channel[2].sample_length = (channel[2].duration * 44100)/1000; }

			//Or stop sound after duration has been met
			else { channel[2].sample_length = 0; stream[x] = -32768; channel[2].playing = false; }
		}
	}

	//Otherwise, generate silence
	else 
	{
		for(int x = 0; x < length; x++) { stream[x] = -32768; }
	}
}

/****** Execute APU operations ******/
void APU::step()
{
	//Check if sound was possibly triggered
	if(mem_link->apu_update_channel)
	{
		mem_link->apu_update_channel = false;

		switch(mem_link->apu_update_addr)
		{
			//Update Sound Channel 1
			case 0xFF10:
			case 0xFF11:
			case 0xFF12:
			case 0xFF13:
				update_channel_1(mem_link->apu_update_addr);
				break;

			//Try to play Sound Channel 1
			case 0xFF14:
				play_channel_1();
				break;
			
			//Try to play Sound Channel 2
			case 0xFF19:
				play_channel_2();
				break;

			//Try to play Sound Channel 3
			case 0xFF1E:
				play_channel_3();
				break;
		}
	}
}				

/****** SDL Audio Callback ******/ 
void audio_callback(void* _apu, u8 *_stream, int _length)
{
	s16* stream = (s16*) _stream;
	int length = _length/2;

	s16 channel_1_stream[length];
	s16 channel_2_stream[length];
	s16 channel_3_stream[length];

	APU* apu_link = (APU*) _apu;
	apu_link->generate_channel_1_samples(channel_1_stream, length);
	apu_link->generate_channel_2_samples(channel_2_stream, length);
	apu_link->generate_channel_3_samples(channel_3_stream, length);

	SDL_MixAudio((u8*)stream, (u8*)channel_1_stream, length*2, SDL_MIX_MAXVOLUME/16);
	SDL_MixAudio((u8*)stream, (u8*)channel_2_stream, length*2, SDL_MIX_MAXVOLUME/16);
	SDL_MixAudio((u8*)stream, (u8*)channel_3_stream, length*2, SDL_MIX_MAXVOLUME/16);
}
