//==============================================================================
//=>                         SFemtoZ v0.0.2
//=>                        www.microdrum.net
//=>                         CC BY-NC-SA 3.0
//=>
//=> Massimo Bernava
//=> massimo.bernava@gmail.com
//=> 2019-12-04
//==============================================================================

#define _GNU_SOURCE
//#include <jack/jack.h>
//#include <jack/midiport.h>
//#include <alsa/asoundlib.h> 
#include "rtmidi_c.h"
#include <soundio/soundio.h>
#include <sndfile.h>
#include <wiringSerial.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>

#define USBPATH "/media/usb/"
#define MAX_POLYPHONY  50
#define FRAME_COUNT 128
#define FALSE 0
#define TRUE  1
#define BARWIDTH 50
#define BARRESOLUTION 5

typedef struct
{
	float *buffer;
	int size;
	
	char* name;

	//List
	struct snd* next;
} snd;

typedef struct
{
	snd *sound;
	int pos;
	int volume;
} plsnd;

typedef struct
{
	snd *sound;
	char *sample;
	int volume;
	int key;
	int lokey;
	int hikey;
	int lovel;
	int hivel;
	int off_mode;
	int off_by;
	float lorand;
	float hirand;
	int group;
	int cc; //only one
	int locc;
	int hicc;

	//List
	struct sfzreg* next;
} sfzreg;

plsnd playingsounds[MAX_POLYPHONY];
int playIndex=0;
snd* sounds=NULL;
sfzreg* regions;
int run;
int nregions=0;
//int lastcckey=0;
//int lastccvalue=0;
char cc[127];
int verbose=FALSE;
int useusb=FALSE;
int backend=FALSE;
unsigned long totMemSamples=0;

//===SOUNDIO=============
struct SoundIo *soundio;
struct SoundIoDevice *device;
struct SoundIoOutStream *outstream;
//=======================

//===RTMIDI=============
RtMidiInPtr midiin = NULL;
//=======================

//========AUDIO CALLBACK=============
static void (*write_sample)(char *ptr, double sample);

static void write_sample_s16ne(char *ptr, double sample) {
    int16_t *buf = (int16_t *)ptr;
    double range = (double)INT16_MAX - (double)INT16_MIN;
    double val = sample * range / 2.0;
    *buf = val;
}
static void write_sample_s32ne(char *ptr, double sample) {
    int32_t *buf = (int32_t *)ptr;
    double range = (double)INT32_MAX - (double)INT32_MIN;
    double val = sample * range / 2.0;
    *buf = val;
}
static void write_sample_float32ne(char *ptr, double sample) {
    float *buf = (float *)ptr;
    *buf = sample;
}
static void write_sample_float64ne(char *ptr, double sample) {
    double *buf = (double *)ptr;
    *buf = sample;
}

//static double seconds_offset = 0.0;

static void write_callback(struct SoundIoOutStream *outstream, int frame_count_min, int frame_count_max)
{
	int frames_left = frame_count_max;//FRAME_COUNT;
	//int channels=outstream->layout.channel_count;
	//printf("min:%i max:%i\n",frame_count_min,frame_count_max);
	
	/*double PI = 3.14159265358979323846264338328;
	double float_sample_rate = outstream->sample_rate;
    double seconds_per_frame = 1.0 / float_sample_rate;
    double pitch = 440.0;
    double radians_per_second = pitch * 2.0 * PI;*/
        
	int err;
	struct SoundIoChannelArea *areas;

	while(frames_left>0)
	{
		int frame_count = frames_left;

		if((err = soundio_outstream_begin_write(outstream,&areas,&frame_count)))
		{
			printf("soundio_outstream_begin_write ERROR: %s framecount(%i)\n",soundio_strerror(err),frame_count);
			exit(1);
		}
		if(!frame_count) break;

        for(int frame = 0; frame < frame_count; frame +=1)
        {
			double value1=0;
			double value2=0;
			int n=0; //number of sounds
			for(int i=0;i<MAX_POLYPHONY;i++)
			{
				if(playingsounds[i].sound!=NULL)
				{
					n++;
					value1 += playingsounds[i].sound->buffer[playingsounds[i].pos];//*playingsounds[i].volume;
					value2 += playingsounds[i].sound->buffer[playingsounds[i].pos+1];//*playingsounds[i].volume;
					playingsounds[i].pos+=2;
					if(playingsounds[i].pos>=playingsounds[i].sound->size)
						playingsounds[i].sound=NULL;
				}
			}
			//double sample = sin((seconds_offset + frame * seconds_per_frame) * radians_per_second);
            
            
			write_sample(areas[0].ptr, value1);
            areas[0].ptr += areas[0].step;
            write_sample(areas[1].ptr, value2);
            areas[1].ptr += areas[1].step;
            
            
			/*short *ptr1=(short*)(areas[0].ptr + areas[0].step*frame);
			short *ptr2=(short*)(areas[1].ptr + areas[1].step*frame);
			if(n>0)
			{
				//if (n>10) printf("Poly:%i\n",n);
				*ptr1=(short)(value1/127);
				*ptr2=(short)(value2/127);
			}
			else
			{
				*ptr1=0;
				*ptr2=0;
			}*/
		}
		//seconds_offset = fmod(seconds_offset + seconds_per_frame * frame_count, 1.0);
            

		if ((err = soundio_outstream_end_write(outstream)))
		{
			printf("soundio_outstream_end_write ERROR: %s \n",soundio_strerror(err));
			exit(1);
		}

		frames_left -= frame_count;
	}
}

//==============================================================================
//   SOUND
//==============================================================================
void loadBar(int x,int n,int r,int w)
{
	if(x % (n/r + 1) != 0 ) return;
    float ratio = x/(float)n;
    int c = ratio*w;

    printf("%3d%% [", (int)(ratio*100));
    for(int x=0;x<c;x++) printf("=");
    for(int x=c;x<w;x++) printf(" ");

    printf("]\n\033[F\033[J");
}

char *trimwhitespace(char *str)
{
  char *end;

  // Trim leading space
  while(isspace(*str)) str++;

  if(*str == 0)  // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace(*end)) end--;

  // Write new null terminator
  *(end+1) = 0;

  return str;
}

void freesounds()
{
	while(sounds!=NULL)
	{
		snd* tmp=(snd*)sounds->next;
		free(sounds->buffer);
		free(sounds);
		sounds=tmp;
	}
}

snd* addsound(char *filename)
{
	SNDFILE *sndfile;
	SF_INFO sfinfo;
	int readcount,subformat;

	//printf("Load sound:%s",filename);

	if(!(sndfile = sf_open(filename,SFM_READ,&sfinfo)))
	{
		printf("sf_open(%s) ERROR: %s\n",filename,sf_strerror(NULL));
		return NULL;
	}
	if(sfinfo.channels<1 || sfinfo.channels>2)
	{
		printf("Channels ERROR!\n");
		return NULL;
	}
	
	subformat = sfinfo.format & SF_FORMAT_SUBMASK;

	//if(verbose==TRUE) printf("File: %s Format: 0x%08X\n",filename, sfinfo.format);
	
	snd* tmp=(snd*)malloc(sizeof(snd));

	tmp->size=sfinfo.frames*sfinfo.channels;
        tmp->buffer=malloc(tmp->size*sizeof(float));
	tmp->next=NULL;

	readcount = /*sf_read_short*/sf_readf_float(sndfile,tmp->buffer,tmp->size);

	if(sounds==NULL)
	{
		sounds=tmp;
	}
	else
	{
		snd* current=sounds;
		while(current->next!=NULL) current=(snd*)current->next;
		current->next=(struct snd*)tmp;
	}

        sf_close(sndfile);

	return tmp;
}


void loadsounds()
{
	//printf("loadsound\n");
	totMemSamples=0;
    sfzreg* r=regions;
	printf("\n");
	int x=0;
    while(r!=NULL)
    {
		//TODO evita i duplicati
        if(r->sample!=NULL) r->sound=addsound(trimwhitespace(r->sample));
        //printf("Load: %s\n",r->sound);
        if(r->sound!=NULL) totMemSamples+=r->sound->size*sizeof(float);
        
        if((totMemSamples/1000000)>2500) break;
        
        r=(sfzreg*)r->next;
		loadBar(x,nregions,BARRESOLUTION,BARWIDTH);
		x++;
    }
	printf("TOTMEM: %3d MiB\n",totMemSamples/1000000);
}

int midiCC(int key,int value)
{
	if(verbose==TRUE) printf("CC(%i,%i)\n",key,value);
	cc[key]=value;
}

int noteOn(int key,int vel)
{
	sfzreg* r=regions;
	float rnd=(float)rand()/(float)(RAND_MAX);

    	while(r!=NULL)
    	{
	        if((key==r->key || (key>=r->lokey && key<=r->hikey))&&
		   r->lovel<=vel && vel<=r->hivel &&
		   r->lorand<=rnd && rnd<=r->hirand &&
		   (r->cc==-1 ||  (r->locc<=cc[r->cc] && cc[r->cc]<=r->hicc))
		)
		{
			playingsounds[playIndex].sound=r->sound;
			playingsounds[playIndex].pos=0;
			playingsounds[playIndex].volume=vel;
			playIndex=(playIndex+1)%MAX_POLYPHONY;
			//printf("<region> sample=%s volume=%i key=%i locc%i=%i hicc%i=%i\n",r->sample,r->volume,r->key,r->cc,r->locc,r->cc,r->hicc);
			if(verbose==TRUE) printf("SOUND: sample=%s key=%i vel=%i rand=%f\n",r->sample,key,vel,rnd);
			return 1;
		}
        	r=(sfzreg*)r->next;
    	}
	if(verbose==TRUE) printf("NO SOUND: key=%i vel=%i rand=%f\n",key,vel,rnd);
	return 0;
}

//==============================================================================
//   SOUNDIO
//==============================================================================

static void print_channel_layout(const struct SoundIoChannelLayout *layout) {
    if (layout->name) {
        fprintf(stderr, "%s", layout->name);
    } else {
        fprintf(stderr, "%s", soundio_get_channel_name(layout->channels[0]));
        for (int i = 1; i < layout->channel_count; i += 1) {
            fprintf(stderr, ", %s", soundio_get_channel_name(layout->channels[i]));
        }
    }
}

static void print_device(struct SoundIoDevice *device, bool is_default) {
    const char *default_str = is_default ? " (default)" : "";
    const char *raw_str = device->is_raw ? " (raw)" : "";
    fprintf(stderr, "%s%s%s\n", device->name, default_str, raw_str);
    //if (short_output)
     //   return;
    fprintf(stderr, "  id: %s\n", device->id);
    if (device->probe_error) {
        fprintf(stderr, "  probe error: %s\n", soundio_strerror(device->probe_error));
    } else {
        fprintf(stderr, "  channel layouts:\n");
        for (int i = 0; i < device->layout_count; i += 1) {
            fprintf(stderr, "    ");
            print_channel_layout(&device->layouts[i]);
            fprintf(stderr, "\n");
        }
        if (device->current_layout.channel_count > 0) {
            fprintf(stderr, "  current layout: ");
            print_channel_layout(&device->current_layout);
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "  sample rates:\n");
        for (int i = 0; i < device->sample_rate_count; i += 1) {
            struct SoundIoSampleRateRange *range = &device->sample_rates[i];
            fprintf(stderr, "    %d - %d\n", range->min, range->max);
        }
        if (device->sample_rate_current)
            fprintf(stderr, "  current sample rate: %d\n", device->sample_rate_current);
        fprintf(stderr, "  formats: ");
        for (int i = 0; i < device->format_count; i += 1) {
            const char *comma = (i == device->format_count - 1) ? "" : ", ";
            fprintf(stderr, "%s%s", soundio_format_string(device->formats[i]), comma);
        }
        fprintf(stderr, "\n");
        if (device->current_format != SoundIoFormatInvalid)
            fprintf(stderr, "  current format: %s\n", soundio_format_string(device->current_format));
        fprintf(stderr, "  min software latency: %0.8f sec\n", device->software_latency_min);
        fprintf(stderr, "  max software latency: %0.8f sec\n", device->software_latency_max);
        if (device->software_latency_current != 0.0)
            fprintf(stderr, "  current software latency: %0.8f sec\n", device->software_latency_current);
    }
    fprintf(stderr, "\n");
}

int list_devices(struct SoundIo *soundio) {
    int output_count = soundio_output_device_count(soundio);
    int input_count = soundio_input_device_count(soundio);
    int default_output = soundio_default_output_device_index(soundio);
    int default_input = soundio_default_input_device_index(soundio);
    fprintf(stderr, "--------Input Devices--------\n\n");
    for (int i = 0; i < input_count; i += 1) {
        struct SoundIoDevice *device = soundio_get_input_device(soundio, i);
        print_device(device, default_input == i);
        soundio_device_unref(device);
    }
    fprintf(stderr, "\n--------Output Devices--------\n\n");
    for (int i = 0; i < output_count; i += 1) {
        struct SoundIoDevice *device = soundio_get_output_device(soundio, i);
        print_device(device, default_output == i);
        soundio_device_unref(device);
    }
    fprintf(stderr, "\n%d devices found\n", input_count + output_count);
    return 0;
}

int configuresoundio()
{
	int err;
	soundio = soundio_create();

	if(!soundio)
	{
		printf("soundio_create ERROR\n");
		return 1;
	}

	if(backend)
	{
		int count_backend=soundio_backend_count(soundio);
		enum SoundIoBackend backend[5];
		for(int index=0;index<count_backend;index++)
		{
			backend[index]=soundio_get_backend(soundio,index); 
			fprintf(stderr, "%i) %s\n",index, soundio_backend_name(backend[index]));
		}
		printf("Select backend:");
		char b=getc(stdin);

		if((err = soundio_connect_backend(soundio,backend[b-'0'])))
		{
			fprintf(stderr, "soundio_connect ERROR: %s\n", soundio_strerror(err));
			return 1;
		}
	}
	else if((err = soundio_connect(soundio)))
	{
		fprintf(stderr, "soundio_connect ERROR: %s\n", soundio_strerror(err));
		return 1;
	}

	soundio_flush_events(soundio);

	list_devices(soundio);

	int default_out_device_index = soundio_default_output_device_index(soundio);
	if (default_out_device_index < 0)
	{
		printf("soundio_default_output_device_index ERROR!\n");
		return 1;
	}

	device = soundio_get_output_device(soundio,default_out_device_index);
	if(!device)
	{
		printf("soundio_get_output_device ERROR!\n");
		return 1;
	}

	printf("Output device: %s\n",device->name);

	outstream = soundio_outstream_create(device);
	//outstream->format = SoundIoFormatFloat32LE;// SoundIoFormatS16NE; //
	outstream->write_callback = write_callback;
	outstream->software_latency = 0.001;

	if (soundio_device_supports_format(device, SoundIoFormatFloat32NE)) {
        outstream->format = SoundIoFormatFloat32NE;
        write_sample = write_sample_float32ne;
    } else if (soundio_device_supports_format(device, SoundIoFormatFloat64NE)) {
        outstream->format = SoundIoFormatFloat64NE;
        write_sample = write_sample_float64ne;
    } else if (soundio_device_supports_format(device, SoundIoFormatS32NE)) {
        outstream->format = SoundIoFormatS32NE;
        write_sample = write_sample_s32ne;
    } else if (soundio_device_supports_format(device, SoundIoFormatS16NE)) {
        outstream->format = SoundIoFormatS16NE;
        write_sample = write_sample_s16ne;
    } else {
        fprintf(stderr, "No suitable device format available.\n");
        return 1;
    }
    
	if((err = soundio_outstream_open(outstream)))
	{
		printf("sound_outstream_open ERROR!\n");
		return 1;
	}
	if(outstream->layout_error)
		printf("outstream->layout_error");

	if((err = soundio_outstream_start(outstream)))
	{
		printf("soundio_outstream_start ERROR!\n");
		return 1;
	}

	return 0;
}

//==============================================================================
//   SFZ
//==============================================================================
sfzreg* addregion(sfzreg *region)
{
	if(region==NULL) return NULL;
	sfzreg* tmp=(sfzreg*)malloc(sizeof(sfzreg));
	if(tmp==NULL) return NULL;
        //memcpy(tmp,region,sizeof(sfzreg));

	nregions++;

	tmp->key=region->key;
	tmp->lokey=region->lokey;
	tmp->hikey=region->hikey;
	tmp->volume=region->volume;
	tmp->sample=region->sample;
	tmp->lovel=region->lovel;
	tmp->hivel=region->hivel;
	tmp->lorand=region->lorand;
	tmp->hirand=region->hirand;
	tmp->group=region->group;
	tmp->off_by=region->off_by;
	tmp->cc=region->cc;
	tmp->locc=region->locc;
	tmp->hicc=region->hicc;
	tmp->next=NULL;
        if(regions==NULL)
        {
                regions=tmp;
        }
        else
        {
                sfzreg* current=regions;
                while(current->next!=NULL) current=(sfzreg*)current->next;
                current->next=(struct sfzreg*)tmp;
        }

        return tmp;
}

void freesfz()
{
	while(regions!=NULL)
        {
                sfzreg* tmp=(sfzreg*)regions->next;
                free(regions->sample);
                free(regions);
                regions=tmp;
        }
}

void printsfz()
{
	sfzreg* r=regions;
	while(r!=NULL)
	{
		printf("<region> sample=%s volume=%i key=%i lovel=%i hivel=%i lorand=%f hirand=%f group=%i off_by=%i locc%i=%i hicc%i=%i\n",
		r->sample,r->volume,r->key,r->lovel,r->hivel,r->lorand,r->hirand,r->group,r->off_by,r->cc,r->locc,r->cc,r->hicc);
		r=(sfzreg*)r->next;
	}
}

long int strtomidi(char* str)
{
	long int key=strtol(str,NULL,10);
	if(key==0)
	{
		//printf("%s ->",str);
		long int note=(str[0]-'c')*2;
		str[0]=' ';
		if(str[1]=='#') { note++; str[1]=' '; }
		long int octave=(2+strtol(str,NULL,10))*12;
		key=note+octave;
		//printf("%i\n",key);
	}
	return key;
}

void loadsfz(char *filename)
{
	FILE *fp;
	char* line = NULL;
	char* path = strdup(filename);
	for(int i=strlen(path)-1;i>=0;i--) if(path[i]=='/') {path[i]=0; break;}
	size_t len = 0;
	ssize_t read;
	char currentNode='n';// 'n'=NULL 'g'=GROUP 'r'=REGION
	sfzreg current_group;
	sfzreg current_region;
	fp = fopen(filename,"r");

	nregions=0;

	while((read = getline(&line,&len,fp)) !=-1)
	{
		//for(token = strtok(line," "); token;token = strtok(NULL," "))
		//printf("Line: %s",line);
		char* tmp=line;
		char* token;
		while((token = strsep(&tmp," \t\r\n"))!=NULL)
		{
			if(strlen(token)<3 || (token[0]=='/' && token[1]=='/')) break;
			//printf("%s\n",token);
			if(strcmp(token,"<group>")==0)
			{
				if(currentNode=='r')
				{
					//SAVE
					addregion(&current_region);
					//printf("<region> sample=%s volume=%i key=%i\n",current_region.sample,current_region.volume,current_region.key);
				}
				current_group.volume=0;
				current_group.key=0;
				current_group.lokey=0;
				current_group.hikey=0;
				current_group.lovel=0;
				current_group.hivel=255;
				current_group.lorand=0;
				current_group.hirand=1;
				current_group.group=-1;
				current_group.off_by=-1;
				current_group.cc=-1;
				current_group.locc=0;
				current_group.hicc=0;
				current_group.sample=NULL;
				currentNode='g';
			}
			else if(strcmp(token,"<region>")==0)
			{
				if(currentNode=='r')
				{
					//SAVE
					addregion(&current_region);
					//printf("<region> sample=%s volume=%i key=%i\n",current_region.sample,current_region.volume,current_region.key);
				}
				current_region.volume=current_group.volume;
				current_region.key=current_group.key;
				current_region.lokey=current_group.lokey;
				current_region.hikey=current_group.hikey;
				current_region.lovel=current_group.lovel;
				current_region.hivel=current_group.hivel;
				current_region.lorand=current_group.lorand;
				current_region.hirand=current_group.hirand;
				current_region.group=current_group.group;
				current_region.off_by=current_group.off_by;
				current_region.cc=current_group.cc;
				current_region.locc=current_group.locc;
				current_region.hicc=current_group.hicc;
				current_region.sample=NULL;
				currentNode='r';
			}
			else
			{
				//printf("%s\n",token);
				char* opcode=strtok(token,"=");
				char* value=strtok(NULL,"=");
				if(strcmp(opcode,"volume")==0)
				{
					switch(currentNode)
					{
						case 'g': current_group.volume=strtol(value,NULL,10); break;
						case 'r': current_region.volume=strtol(value,NULL,10); break;
					}
				}
				else if(strcmp(opcode,"key")==0)
				{
					switch(currentNode)
					{
						case 'g': current_group.key=strtomidi(value); break;
						case 'r': current_region.key=strtomidi(value); break;
					}
				}
				else if(strcmp(opcode,"lokey")==0)
				{
					switch(currentNode)
					{
						case 'g': current_group.lokey=strtomidi(value); break;
						case 'r': current_region.lokey=strtomidi(value); break;
					}
				}
				else if(strcmp(opcode,"hikey")==0)
				{
					switch(currentNode)
					{
						case 'g': current_group.hikey=strtomidi(value); break;
						case 'r': current_region.hikey=strtomidi(value); break;
					}
				}
				else if(strcmp(opcode,"lovel")==0)
				{
					switch(currentNode)
					{
						case 'g': current_group.lovel=strtol(value,NULL,10);break;
						case 'r': current_region.lovel=strtol(value,NULL,10);break;
					}
				}
				else if(strcmp(opcode,"hivel")==0)
				{
					switch(currentNode)
					{
						case 'g': current_group.hivel=strtol(value,NULL,10);break;
						case 'r': current_region.hivel=strtol(value,NULL,10);break;
					}
				}
				else if(strcmp(opcode,"lorand")==0)
				{
					switch(currentNode)
                                        {
                                                case 'g': current_group.lorand=strtof(value,NULL); break;
                                                case 'r': current_region.lorand=strtof(value,NULL); break;
                                        }
				}
				else if(strcmp(opcode,"hirand")==0)
                {
				    switch(currentNode)
                    {
						case 'g': current_group.hirand=strtof(value,NULL); break;
						case 'r': current_region.hirand=strtof(value,NULL); break;
                    }
				}
				else if(strcmp(opcode,"group")==0)
				{
                                        switch(currentNode)
                                        {
                                                case 'g': current_group.group=strtol(value,NULL,10);break;
                                                case 'r': current_region.group=strtol(value,NULL,10);break;
                                        }
				}
				else if(strcmp(opcode,"off_by")==0)
				{
                                        switch(currentNode)
                                        {
                                                case 'g': current_group.off_by=strtol(value,NULL,10);break;
                                                case 'r': current_region.off_by=strtol(value,NULL,10);break;
                                        }

				}
				else if(strcmp(opcode,"off_mode")==0)
				{

				}
				else if(strcmp(opcode,"loop_mode")==0)
				{

				}
				else if(strncmp(opcode, "locc",4)==0)
				{
					int cc = strtol(opcode+4,NULL,10);
					int locc = strtol(value,NULL,10);
					//printf("locc%i=%i\n",cc,locc);
					switch(currentNode)
					{
						case 'g': current_group.cc=cc; current_group.locc=locc; break;
						case 'r': current_region.cc=cc; current_region.locc=locc; break;
					}
				}
				else if(strncmp(opcode,"hicc",4)==0)
				{
					int cc = strtol(opcode+4,NULL,10);
					int hicc = strtol(value,NULL,10);
					//printf("hicc%i=%i\n",cc,hicc);
					switch(currentNode)
					{
						case 'g': current_group.cc=cc; current_group.hicc=hicc; break;
						case 'r': current_region.cc=cc; current_region.hicc=hicc; break;
					}
				}
				else if(strcmp(opcode,"sample")==0)
				{
					//char* path=dirname(filename);
					//char* file=strdup(value);
					current_region.sample=malloc(strlen(path)+strlen(value)+2);
					strcpy(current_region.sample,path);
					strcat(current_region.sample,"/");
					strcat(current_region.sample,value);
					//printf("Sample :%s",current_region.sample);
					for(int i=0;i<strlen(current_region.sample);i++) if(current_region.sample[i]=='\\') current_region.sample[i]='/';
				}
			}
		}
	}
	free(path);
	free(line);
	fclose(fp);
}

//==============================================================================
//   TEST
//==============================================================================
void* thread_test(void* arg)
{
	int v=1;
	int n=strtol(arg,NULL,10);
	while(1)
	{
		sleep(1);
		if(run)
		{
			noteOn(n,v);
			printf("midiOn(%i,%i)\n",n,v);
			v=(v+2)%127;
		}
	}

	return NULL;
}

//==============================================================================
//   RTMIDI
//==============================================================================

  
void midi_callback(double timeStamp, const unsigned char* message,
                                 size_t messageSize, void *userData)
{
	switch (message[0] & 0xf0)
              {
                case 0x90: noteOn(message[1],message[2]);
                           break;
                case 0x80: //synth.add_event_note_off (in_event.time, channel, in_event.buffer[1]);
                           break;
                case 0xb0: midiCC(message[1],message[2]); 
                           break;
              }
}

//==============================================================================
//   EXIT_CLI
//==============================================================================
void exit_cli(int sig)
{
	run = FALSE;
	printf("\rsfemtoz closing down...\n\n");
}

//==============================================================================
//   PRINTHELP
//==============================================================================
void printhelp()
{
	printf("Usage: sfemtoz [tvhu] file.sfz \n");
	printf("Options:\n");
	printf("\tv: verbose mode\n");
	printf("\th: show this help\n");
	printf("\tu: use USB path\n");
	printf("\tt note: test note\n");
	printf("\n\n");
}

//==============================================================================
//   MAIN
//==============================================================================
int main(int argc,char **argv)
{
	pthread_t test;

	printf("SFemtoZ!\n");

	int c;
	while ((c = getopt (argc, argv, "t:vhubjm")) != -1)
    		switch (c)
		{
			case 't': //testnote optarg=midinote
				if(pthread_create(&test,NULL,thread_test,optarg))
				{
					printf("error thread test\n");
				}
			break;

			case 'v': //verbose mode
				verbose=TRUE;
			break;

			case 'h': //print help
				printhelp();
				return 0;
			break;

			case 'u': //use usb path
				useusb=TRUE;
			break;

			case 'b'://select backend
				backend=TRUE;
			break;
			
			/*case 'j'://start jack
				if(client==NULL) client = jack_client_open ("SFemtoZ!", JackNullOption, NULL);
			break;*/
			
			case 'm'://midi
				
				 midiin=rtmidi_in_create_default();
				 int port_count=rtmidi_get_port_count(midiin);
				 printf("MIDI Port Count: %i\n",port_count);
				 
				  for (int i = 0; i < port_count; i ++) 
				  {
					  printf("%i) %s \n",i,rtmidi_get_port_name(midiin,i));
				  }
				  
				  rtmidi_open_port (midiin, 1, "test");
				  rtmidi_in_set_callback (midiin,&midi_callback,0);
			break;

			case '?':
				if (optopt == 't')
          				fprintf (stderr, "Option -%c requires MIDI Note.\n", optopt);
        			else if (isprint (optopt))
          				fprintf (stderr, "Unknown option `-%c'.\n", optopt);
        			else
          				fprintf (stderr,
                   			"Unknown option character `\\x%x'.\n",
                   			optopt);
        			return 1;
			default:
				abort();


		}

	if(optind>=argc)
	{
		printhelp();
		return 0;
	}

	char sfzfile[50];
	if(useusb)
	{	
		strcpy(sfzfile,USBPATH);
		strcat(sfzfile,argv[optind]);
	}
	else
		strcpy(sfzfile,argv[optind]);

	printf("Load SFZ: %s\n",sfzfile);
	loadsfz(sfzfile);
	//printsfz();
	loadsounds();

	int fd;
	if(( fd = serialOpen("/dev/ttyAMA0",57600))<0)
	{
		printf("serialOpen ERROR:\n",strerror(errno));
		return 1;
	}
	if(configuresoundio()!=0) return 1;

	signal(SIGINT,exit_cli);
	signal(SIGTERM,exit_cli);

	run = TRUE;

	while(run)
	{
		if(serialDataAvail(fd)>2)
		{
			int type=serialGetchar(fd);
			while(type!=0x99 && type!= 0xB9) type=serialGetchar(fd);

			int note=serialGetchar(fd);
			int vel=serialGetchar(fd);
			//printf("MIDI%i(%i,%i)\n",type,note,vel);
			if(type==0x99) noteOn(note,vel);
			else midiCC(note,vel);
		}

//		soundio_wait_events(soundio);
	}

	//FINISH
	pthread_cancel(test);

	soundio_outstream_destroy(outstream);
	soundio_device_unref(device);
	soundio_destroy(soundio);

	serialClose(fd);
	
	//if(client!=NULL) jack_client_close (client);
	if(midiin!=NULL) rtmidi_in_free(midiin);

	freesounds();
	freesfz();

	return 0;
}
