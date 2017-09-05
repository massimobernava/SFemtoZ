//==============================================================================
//=>                         SFemtoZ v1.0.0
//=>                        www.microdrum.net
//=>                         CC BY-NC-SA 3.0
//=>
//=> Massimo Bernava
//=> massimo.bernava@gmail.com
//=> 2016-01-07
//==============================================================================

#define _GNU_SOURCE
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

#define USBPATH "/media/usb/"
#define MAX_POLYPHONY  50
#define FRAME_COUNT 128
#define FALSE 0
#define TRUE  1
#define BARWIDTH 50
#define BARRESOLUTION 5

typedef struct
{
	short *buffer;
	int size;

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

//===SOUNDIO=============
struct SoundIo *soundio;
struct SoundIoDevice *device;
struct SoundIoOutStream *outstream;
//=======================

//========AUDIO CALLBACK=============
static void write_callback(struct SoundIoOutStream *outstream, int frame_count_min, int frame_count_max)
{
	int frames_left = FRAME_COUNT;//frame_count_max;
	//int channels=outstream->layout.channel_count;
	//printf("min:%i max:%i\n",frame_count_min,frame_count_max);

	int err;
	struct SoundIoChannelArea *areas;

	while(frames_left>0)
	{
		int frame_count = frames_left;

		if((err = soundio_outstream_begin_write(outstream,&areas,&frame_count)))
		{
			printf("soundio_outstream_begin_write ERROR: %s",soundio_strerror(err));
			exit(1);
		}
		if(!frame_count) break;

                for(int frame = 0; frame < frame_count; frame +=1)
                {
			int value1=0;
			int value2=0;
			int n=0;
			for(int i=0;i<MAX_POLYPHONY;i++)
			{
				if(playingsounds[i].sound!=NULL)
				{
					n++;
					value1 += playingsounds[i].sound->buffer[playingsounds[i].pos]*playingsounds[i].volume;
					value2 += playingsounds[i].sound->buffer[playingsounds[i].pos+1]*playingsounds[i].volume;
					playingsounds[i].pos+=2;
					if(playingsounds[i].pos>=playingsounds[i].sound->size)
						playingsounds[i].sound=NULL;
				}
			}
			short *ptr1=(short*)(areas[0].ptr + areas[0].step*frame);
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
			}
		}

		if ((err = soundio_outstream_end_write(outstream)))
		{
			printf("soundio_outstream_end_write ERROR: %s",soundio_strerror(err));
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

	snd* tmp=(snd*)malloc(sizeof(snd));

	tmp->size=sfinfo.frames*sfinfo.channels;
        tmp->buffer=malloc(tmp->size*sizeof(short));
	tmp->next=NULL;

	readcount = sf_read_short(sndfile,tmp->buffer,tmp->size);

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
        sfzreg* r=regions;
	printf("\n");
	int x=0;
        while(r!=NULL)
        {
		//TODO evita i duplicati
                if(r->sample!=NULL) r->sound=addsound(trimwhitespace(r->sample));
                r=(sfzreg*)r->next;
		loadBar(x,nregions,BARRESOLUTION,BARWIDTH);
		x++;
        }
	printf("\n");
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
	        if(key==r->key &&
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
			if(verbose==TRUE) printf("SOUND: sample=%s key=%i vel=%i rend=%f\n",r->sample,key,vel,rnd);
			return 1;
		}
        	r=(sfzreg*)r->next;
    	}
	if(verbose==TRUE) printf("NO SOUND: key=%i vel=%i rand=%f\n",key,vel,rnd);
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

	//if((err = soundio_connect(soundio)))
	if((err = soundio_connect_backend(soundio,SoundIoBackendAlsa)))
	{
		printf("soundio_connect ERROR\n");
		return 1;
	}

	soundio_flush_events(soundio);

	int default_out_device_index = soundio_default_output_device_index(soundio);
	if (default_out_device_index < 0)
	{
		printf("soundio_default_output_device_index ERROR!");
		return 1;
	}

	device = soundio_get_output_device(soundio,default_out_device_index);
	if(!device)
	{
		printf("soundio_get_output_device ERROR!");
		return 1;
	}

	printf("Output device: %s\n",device->name);

	outstream = soundio_outstream_create(device);
	outstream->format = SoundIoFormatS16NE; //SoundIoFormatFloat32NE;
	outstream->write_callback = write_callback;
	outstream->software_latency = 0.001;

	if((err = soundio_outstream_open(outstream)))
	{
		printf("sound_outstream_open ERROR!");
		return 1;
	}
	if(outstream->layout_error)
		printf("outstream->layout_error");

	if((err = soundio_outstream_start(outstream)))
	{
		printf("soundio_outstrea_start ERROR!");
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
		while((token = strsep(&tmp," \t"))!=NULL)
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
				current_group.lovel=0;
				current_group.hivel=0;
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
						case 'g': current_group.key=strtol(value,NULL,10); break;
						case 'r': current_region.key=strtol(value,NULL,10); break;
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
			printf("test(%i,%i)\n",n,v);
			v=(v+2)%127;
		}
	}

	return NULL;
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
//   MAIN
//==============================================================================
int main(int argc,char **argv)
{
	pthread_t test;

	printf("SFemtoZ!\n");

	int c;
	while ((c = getopt (argc, argv, "t:v")) != -1)
    		switch (c)
		{
			case 't':
				if(pthread_create(&test,NULL,thread_test,optarg))
				{
					printf("error thread test\n");
				}
			break;

			case 'v':
				verbose=TRUE;
			break;


		}

	char sfzfile[50];
	strcpy(sfzfile,USBPATH);
	strcat(sfzfile,argv[optind]);
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

	freesounds();
	freesfz();

	return 0;
}
