/* 
*	FLV 视频格式分析
*	FLV Format Analysis
*/
// This app splits an FLV file based on cue points located in a data file.
// It also separates the video from the audio.
// All the reverse_bytes action stems from the file format being big-endian
// and needing an integer to hold a little-endian version (for proper calculation).

#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include "flvparse.h"
#include <netinet/in.h>
#include<unistd.h>

#pragma pack(1)

//************ Constants
#define TAG_TYPE_AUDIO 8
#define TAG_TYPE_VIDEO 9
#define TAG_TYPE_SCRIPT 18
#define CUE_BLOCK_SIZE 32
#define FLAG_SEPARATE_AV 1

//*********** TYPEDEFs
typedef unsigned char ui_24[3];
typedef unsigned char byte;
typedef unsigned int uint;
typedef uint FLV_PREV_TAG_SIZE;
#define _MAX_PATH 200
typedef struct {
	byte Signature[3];
	byte Version;
	byte Flags;
	uint DataOffset;
} FLV_HEADER;

typedef struct {
	byte TagType;
	ui_24 DataSize;
	ui_24 Timestamp;
	uint Reserved;
} FLV_TAG;

int fist_time_a=0;
int fist_time_v=0;
char cmd_buf[2000];

//********** local function prototypes
uint copymem(char *destination, char *source, uint byte_count);
uint fget(FILE *filehandle, char *buffer, uint buffer_size);
uint fput(FILE *filehandle, char *buffer, uint buffer_size);
FILE *open_output_file(byte tag_type) ;
void processfile(char *flv_filename, char *cue_file);
uint *read_cue_file(char *cue_file_name);
uint reverse_bytes(byte *buffer, char buffer_size);
uint xfer(FILE *input_file_handle, FILE *output_file_handle, uint byte_count);
uint xfer_empty(FILE *input_file_handle, FILE *output_file_handle, uint byte_count);
int process_audio_tag(unsigned char,char *);

//********* global variables
uint current=0;
uint flags = 0;
char project_name[_MAX_PATH];

int flvparse(int argc, char* argv[]);

int main(int argc, char* argv[])
{
	int ret;

	ret = flvparse(argc, argv);
	if(ret < 0){
		printf("flvparse is fail\n");
	}
	printf("flvparse is right\n");

	return 0;
}
//Defines the entry point for the console application.
int flvparse(int argc, char* argv[])
{
	if (argc < 3){ 
		printf("invalid command line\n");
	}
	else {
		if ((argc==4) && (strstr(argv[3],"-s")!=NULL)) flags |= FLAG_SEPARATE_AV;
		processfile(argv[1], argv[2]);
	}

	return 0;
}
int process_audio_tag(unsigned char tag_header,char *response)
{
	char audio_format[][100]={
		"Linear PCM, platform endian","ADPCM","MP3","Linear PCM, little endian","Nellymoser 16-kHz mono",
		"Nellymoser 8-kHz mono","Nellymoser","G.711 A-law logarithmic PCM","G.711 mu-law logarithmic PCM","reserved",
		"AAC","Speex","MP3 8-Khz","Device-specific sound",
	};
	char audio_sample_rate[][20]={\
		"5.5-kHz","11-kHz","22-kHz","44-kHz"
	};
	char audio_sample_bits[][20]={
		"snd8Bit","snd16Bit"
	};
	char audio_type[][20]={
		"sndMono","sndStereo"
	};
//	printf("%d,%d,%d,%d\n",(tag_header&0xf0)>>4,(tag_header&0x0c)>>2,(tag_header&0x02)>>1,(tag_header&0x01));
	snprintf(response,500,"%s\t|%s\t|%s\t|%s\t|",\
		audio_format[(tag_header&0xf0)>>4],\
		audio_sample_rate[(tag_header&0x0c)>>2],\
		audio_sample_bits[(tag_header&0x02)>>1],\
		audio_type[(tag_header&0x01)]);
	return 0;
}
int process_video_tag(unsigned char tag_header,char *response)
{
	char frame_format[][100]={
		"","keyframe (for AVC, a seekable frame)","inter frame (for AVC, a non-seekable frame)",
		"disposable inter frame (H.263 only)","generated keyframe (reserved for server use only)",
		"video info/command frame"
	};
	char video_enc[][50]={\
		"","JPEG (currently unused)","Sorenson H.263","Screen video","On2 VP6","On2 VP6 with alpha channel",
		"Screen video version 2","AVC"
	};
//	printf("%d,%d\n",(tag_header&0xf0),(tag_header&0x0f));
	snprintf(response,500,"%s\t|%s\t|",\
		frame_format[(tag_header&0xf0)>>4],\
		video_enc[(tag_header&0x0f)]);
	return 0;
}
int process_metadata_tag(char *metatag_buf,int temp_datasize,FILE *cfh)
{
	int p=0;
	int s=0;
	char *amf_str;
	char buf[1024];
	//读取AMF 第一个
	if(metatag_buf[p] == 0x02){
		s=p;
		p++;
		int AMF_len = metatag_buf[p];
		AMF_len = (AMF_len<<8)+metatag_buf[p+1];	
		amf_str = (char *)malloc(AMF_len+1);
		p += 2;
		memcpy(amf_str,&metatag_buf[p],AMF_len);
		amf_str[AMF_len]='\0';
		p += AMF_len;
		
		snprintf(buf,1024,"amf1:\t|type:%d\t|len:%d\t|%s\t|",metatag_buf[s],AMF_len,amf_str);

		free(amf_str);	
		int ret = fwrite(buf,strlen(buf),1,cfh);
		if(ret != 1){
			printf("[%s]:fwrite is error\n",__LINE__);
		}
	}
	if(metatag_buf[p]==0x08){
		s=p;
		p++;
		unsigned int amf2_len = (metatag_buf[p]<<24)|(metatag_buf[p+1]<<16)|(metatag_buf[p+2]<<8)|(metatag_buf[p+3]);
		p += 4;
		printf("amf2_len:[%d]\n",amf2_len);		
		snprintf(buf,1024,"amf2:\t|type:%d\tlen:%d\t|\n",metatag_buf[s],amf2_len);
		int ret = fwrite(buf,strlen(buf),1,cfh);
		if(ret != 1){
			printf("[%s]:fwrite is error\n",__LINE__);
		}
		for(int i=0;i<amf2_len;i++){
			int amf2_node_len = metatag_buf[p]<<8|metatag_buf[p+1];
			p+=2;
			if(amf2_node_len == 0){
				if(metatag_buf[p]==0x00){
					p+=1;
					p+=8;
				}
				else if(metatag_buf[p]==0x01){
					p+=1;
					p+=1;
				}
				continue;
			}

			amf_str = (char *)malloc(amf2_node_len+1);
			memcpy(amf_str,&metatag_buf[p],amf2_node_len);
			amf_str[amf2_node_len]='\0';
			p+=amf2_node_len;
			
			printf("ceq : [%d];type,[%d]\n",i,metatag_buf[p]);
			union{
				double d;
				char a[8];
			}value;
			unsigned char value_b;
			int ret;
			if(metatag_buf[p]==0x00){
				p+=1;
				for(int j=0;j<8;j++){
					printf("%d\t",metatag_buf[p+j]);
					value.a[8-1-j]=metatag_buf[p+j];
				}
				p+=8;
				snprintf(buf,1024,"[%d][%s:%lf]\n",i,amf_str,value.d);	
				ret = fwrite(buf,strlen(buf),1,cfh);
				if(ret != 1){
					printf("[%s]:fwrite is error\n",__LINE__);
				}
			}
			else if(metatag_buf[p]==0x01){
				p+=1;
				value_b = metatag_buf[p];
				p+=1;
				char stereo[][20]={
					"non-stereo","stereo"
				};
				snprintf(buf,1024,"[%d][%s:%s]\n",i,amf_str,stereo[!!value_b]);	
				ret = fwrite(buf,strlen(buf),1,cfh);
				if(ret != 1){
					printf("[%s]:fwrite is error\n",__LINE__);
				}
			}
			else if(metatag_buf[p]==0x02){
				p+=1;
				unsigned int str_len = metatag_buf[0]<<8|metatag_buf[1];
				p+=2;
				char *str_para = (char *)malloc(str_len+1);
				memcpy(str_para,&metatag_buf[p],str_len);
				p+=str_len;
				str_para[str_len]='\0';
				snprintf(buf,1024,"[%d][%s:%s]\n",i,amf_str,str_para);	
				ret = fwrite(buf,strlen(buf),1,cfh);
				if(ret != 1){
					printf("[%s]:fwrite is error\n",__LINE__);
				}
				free(str_para);
			}
			else if(metatag_buf[p]==0x03){
				printf("metadata array data is end\n");
				return 0;
			}
			printf("\n");
			free(amf_str);			
		}		
	}
	return 0;
}

//processfile is the central function
void processfile(char *in_file, char *cue_file){
	//---------
	fist_time_a=0;
	fist_time_v=0;
	//-------------
	FILE *ifh=NULL, *cfh=NULL, *vfh=NULL, *afh = NULL;
	FLV_HEADER flv;
	FLV_TAG tag;
	FLV_PREV_TAG_SIZE pts, pts_z=0;
	uint *cue, ts=0, ts_new=0, ts_offset=0, ptag=0;
	char *metatag_buf;
	
	printf("Processing [%s] with cue file [%s]\n", in_file, cue_file);
	
	//open the input file
	if ( (ifh = fopen(in_file, "rb")) == NULL) {
		//AfxMessageBox("Failed to open files!");
		return;
	}

	//set project name
	strncpy(project_name, in_file, strstr(in_file, ".flv")-in_file);
	
//	//build cue array
//	cue = read_cue_file(cue_file);

	if(!access(cue_file,F_OK)){
		cfh = fopen(cue_file,"wb+");
		fseek(cfh,0,SEEK_SET);
		printf("opwn is succ\n");
	}
	else{
		printf("opwn is fail\n");
	}

	//capture the FLV file header
	//输出Header信息
	fget(ifh, (char *)&flv, sizeof(FLV_HEADER));
	char temp_str[MAX_URL_LENGTH];
	char str_tmp[500];
	
	sprintf(temp_str,"flv header :: \n");
	
	sprintf(str_tmp,"signature : 0x %X %X %X\n",flv.Signature[0],flv.Signature[1],flv.Signature[2]);
	strcat(temp_str,str_tmp);
	
	sprintf(str_tmp,"version : 0x %X\n",flv.Version);
	strcat(temp_str,str_tmp);
	
	sprintf(str_tmp,"flags : 0x %X\n",flv.Flags);
	strcat(temp_str,str_tmp);

	flv.DataOffset = reverse_bytes((byte *)&flv.DataOffset, sizeof(flv.DataOffset));
	sprintf(str_tmp,"header size : 0x %X\n",flv.DataOffset);
	strcat(temp_str,str_tmp);
	fwrite(temp_str,strlen(temp_str),1,cfh);
				
	//move the file pointer to the end of the header
	fseek(ifh,flv.DataOffset, SEEK_SET);

	sprintf(temp_str,"ceq\t|type\t|datasize\t|timestamp\t|\n");
	int ceq=0;
	char tag_type[][10]={
			"audio","video","0x0a","0x0b","0x0c","0x0d","0x0e","0x0f",
			"0x10","0x11","script"
	};
	int ret = fwrite(temp_str,strlen(temp_str),1,cfh);
	if(ret != 1){
		printf("fwrite is error\n");
	}
	
	//process each tag in the file
	do {

		//capture the PreviousTagSize integer
		pts = getw(ifh);
		
		char ch = fgetc(ifh);
		if (!feof(ifh)) {	
			//extract the tag from the input file
			fseek(ifh,-1,SEEK_CUR);
			fget(ifh, (char *)&tag, sizeof(FLV_TAG));
			
			int temp_datasize=tag.DataSize[0]*65536+tag.DataSize[1]*256+tag.DataSize[2];
			int temp_timestamp=tag.Timestamp[0]*65536+tag.Timestamp[1]*256+tag.Timestamp[2];		
			snprintf(temp_str,2000,"%d\t|%s\t|%d\t|%d\t|",ceq++,tag_type[tag.TagType-0x08],temp_datasize,temp_timestamp);						
			//set the tag value to select on
			ptag = tag.TagType;			
			//if we are not separating AV, process the audio like video
			if (!(flags && FLAG_SEPARATE_AV)) ptag = TAG_TYPE_VIDEO;			
			//process tag by type
			switch (ptag) {
				case TAG_TYPE_AUDIO:  //we only process like this if we are separating audio into an mp3 file
					//还需要获取TagData的第一个字节---------------------------------
					char temp_tag_f_b;
					fget(ifh,&temp_tag_f_b,1);
					process_audio_tag(temp_tag_f_b,str_tmp);
					strcat(temp_str,str_tmp);					
					//jump past audio tag header byte
					fseek(ifh, temp_datasize-1, SEEK_CUR);  
					strcat(temp_str,"\n");
					ret = fwrite(temp_str,strlen(temp_str),1,cfh);
					if(ret != 1){
						printf("[%s]:fwrite is error\n",__LINE__);
					}
					break;
				case TAG_TYPE_VIDEO:
					//还需要获取TagData的第一个字节---------------------------------						
					fget(ifh,&temp_tag_f_b,1);
					process_video_tag(temp_tag_f_b,str_tmp);
					strcat(temp_str,str_tmp);					
					fseek(ifh, temp_datasize-1, SEEK_CUR);
					strcat(temp_str,"\n");
					ret = fwrite(temp_str,strlen(temp_str),1,cfh);
					if(ret != 1){
						printf("[%s]:fwrite is error\n",__LINE__);
					}
					break;
				case TAG_TYPE_SCRIPT:
					strcat(temp_str,"\n");
					ret = fwrite(temp_str,strlen(temp_str),1,cfh);
					if(ret != 1){
						printf("[%s]:fwrite is error\n",__LINE__);
					}
					metatag_buf = (char*)malloc(temp_datasize);
					fget(ifh,metatag_buf,temp_datasize);
					process_metadata_tag(metatag_buf,temp_datasize,cfh);					
					free(metatag_buf);
					//fseek(ifh, temp_datasize, SEEK_CUR);
					break;
				default:					
					//skip the data of this tag					
					fseek(ifh, temp_datasize, SEEK_CUR);
					
			}			
		}
	} while (!feof(ifh));
	
	//finished...close all file pointers
	fclose(ifh);
	fclose(cfh);
}


//fget - fill a buffer or structure with bytes from a file
uint fget(FILE *fh, char *p, uint s) {
	uint i;
	for (i=0; i<s; i++)
		*(p+i) = (char)fgetc(fh);
	return i;
}

//fput - write a buffer or structure to file
uint fput(FILE *fh, char *p, uint s) {
	uint i;
	for (i=0; i<s; i++)
		fputc(*(p+i), fh);
	return i;
}

//utility function to overwrite memory
uint copymem(char *d, char *s, uint c) {
	uint i;
	for (i=0; i<c; i++)
		*(d+i) = *(s+i);
	return i;
}

//reverse_bytes - turn a BigEndian byte array into a LittleEndian integer
uint reverse_bytes(byte *p, char c) {
	int r = 0;
	int i;
	for (i=0; i<c; i++) 
		r |= ( *(p+i) << (((c-1)*8)-8*i));
	return r;
}

//xfer - transfers *count* bytes from an input file to an output file
uint xfer(FILE *ifh, FILE *ofh, uint c) {
	uint i;
	for (i=0; i<c; i++)
		fputc(fgetc(ifh),ofh);
	return i;
}

uint xfer_empty(FILE *ifh, FILE *ofh, uint c) {
	uint i;
	for (i=0; i<c; i++)
		fgetc(ifh);
	return i;
}

//This function handles iterative file naming and opening
FILE* open_output_file(byte tag) {

	//instantiate two buffers
	char file_name[_MAX_PATH], ext[4];

	//determine the file extension
	strcpy(ext, (tag==TAG_TYPE_AUDIO ? "mp3\0" : "flv\0"));
	
	//build the file name
	sprintf(file_name, "%s_%i.%s", project_name, current, ext);

	//return the file pointer
	return fopen(file_name, "wb");
		
}


