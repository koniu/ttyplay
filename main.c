
/*

  ttyplay: plays audio on the serial port.

  Take audio from TX and GND on serial connector.

  License: GPL

  credits:

    libsamplerate / libsndfile, http://mega-nerd.com

    serial routines copied blindly from Serial Programming HOWTO.
    http://www.tldp.org/HOWTO/Serial-Programming-HOWTO

    Maxim appnote #1870, for a simple diagram of a Sigma-Delta modulator

    hackaday.com
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <strings.h>
#include <sndfile.h>
#include <samplerate.h>

#define SOUND_BUFFER_LEN (0.25) // seconds
#define BAUDRATE 115200
#define SERIAL_BAUDRATE B115200
#define SERIAL_DEVICE "/dev/ttyS0"
#define OUTPUT_SAMPLERATE 48000

int main(int argc, char **argv) {  
  
  SNDFILE *soundfile = NULL;
  SF_INFO info;
  
  SNDFILE *soundfile_out = NULL;
  SF_INFO info_out;

  float integrator=0.0;
  float error_signal=0.0;
  float out=0.0;

  int i,j,k;
  int count;
  int error;

  int ttyfd=-1;
  struct termios oldtio,newtio;
  
  unsigned char outbyte;
  
  SRC_STATE *samplerate;
  SRC_DATA resample_data;
  SRC_STATE *samplerate_out;
  SRC_DATA resample_data_out;

  info.format=0;

  if ((argc < 2) || (argc>3)) {
    fprintf(stderr,"Use like this:\n %s in.wav [out.wav] \n\n\
 Output will go to %s\n\n\
 Specify out.wav to simulate the output instead.\n",argv[0],SERIAL_DEVICE);
    return 1;
  }

  fprintf(stderr,"Playing %s\n",argv[1]);
  
  soundfile=sf_open(argv[1],SFM_READ,&info);
  
  info_out.samplerate=OUTPUT_SAMPLERATE;
  info_out.channels=1;
  info_out.format=SF_FORMAT_PCM_16|SF_FORMAT_WAV;

  if (argc==3) {
    soundfile_out=sf_open(argv[2],SFM_WRITE,&info_out);

    if ((soundfile==NULL) || (soundfile_out==NULL)) {
      fprintf(stderr,"Couldn't open that file. Sorry.\n");
      return 1;
    }
  } else { // we're not writing an output file, try serial I/O

    // blocking I/O blocks forever on open(), dunno why.
    ttyfd = open(SERIAL_DEVICE, O_RDWR|O_NOCTTY|O_NONBLOCK);
    if (ttyfd <0) {perror(SERIAL_DEVICE); exit(-1); }
    tcgetattr(ttyfd,&oldtio); /* save current serial port settings */
    bzero(&newtio, sizeof(newtio)); /* clear struct for new port settings */
    
    cfmakeraw(&newtio);
    if (0!=cfsetospeed(&newtio,SERIAL_BAUDRATE)) { perror(SERIAL_DEVICE); exit(-1); }
    if (0!=tcflush(ttyfd, TCIFLUSH)) { perror(SERIAL_DEVICE); exit(-1); }
    if (0!=tcsetattr(ttyfd,TCSANOW,&newtio)) { perror(SERIAL_DEVICE); exit(-1); }
  }
  
  samplerate=src_new(SRC_LINEAR,1,&error);

  if (error) {
    fprintf(stderr,"Samplerate error. Sorry.\n");
    return 1;
  }
  
  samplerate_out=src_new(SRC_SINC_FASTEST,1,&error);
  
  if (error) {
    fprintf(stderr,"Samplerate error. Sorry.\n");
    return 1;
  }

  resample_data.data_in=(float *)malloc((int)(SOUND_BUFFER_LEN*info.samplerate*info.channels)*sizeof(float));
  resample_data.data_out=(float *)malloc(2*(int)(SOUND_BUFFER_LEN*BAUDRATE)*sizeof(float));
  resample_data.src_ratio=((float)BAUDRATE)/(float)info.samplerate;
  resample_data.end_of_input=0;

  resample_data.output_frames=(int)2*BAUDRATE*SOUND_BUFFER_LEN;

  resample_data_out.data_in=(float *)malloc(2*BAUDRATE*sizeof(float));
  resample_data_out.data_out=(float *)malloc(2*(int)(SOUND_BUFFER_LEN*OUTPUT_SAMPLERATE)*sizeof(float));
  resample_data_out.src_ratio=((float)OUTPUT_SAMPLERATE)/((float)BAUDRATE);
  resample_data_out.end_of_input=0;
  resample_data_out.output_frames=(int)(2*OUTPUT_SAMPLERATE*SOUND_BUFFER_LEN);

  k=0;

  while ((count=sf_readf_float(soundfile,resample_data.data_in,(int)(SOUND_BUFFER_LEN*info.samplerate)))) {

    // make mono
    for (j=0; j<count; j++) {
      resample_data.data_in[j]=resample_data.data_in[j*info.channels];
      for (i=1; i<info.channels; i++) {
	resample_data.data_in[j]+=resample_data.data_in[j*info.channels+i];
      }      
      resample_data.data_in[j]/=(float)info.channels;
    }

    resample_data.input_frames=count;
    
    // following has a bug...
    if (count<(int)(SOUND_BUFFER_LEN*info.samplerate)) {
      resample_data.end_of_input=1;
      resample_data_out.end_of_input=1;
    }
    
    src_process(samplerate,&resample_data);
    
    // Now we have our input signal converted to the serial baudrate.  All that's left is sigma-delta modulate
    // and write to the serial port

    outbyte=0;
    for (j=0; j<resample_data.output_frames_gen; j++) {
 
      error_signal=resample_data.data_out[j]-out;
      
      integrator+=error_signal;

      switch (k%10) {
      case 0:
      	outbyte=0;
	out=-1.0; //start bit
	break;
      case 9:
	out=1.0; //stop bit
	if (ttyfd>=0) {
	  while ( 1!=write(ttyfd,&outbyte,1)) { /* What's a spinloop among friends? */ ; }
	}
	break;
      default:
	out=(integrator>0) ? 1.0 : -1.0;
	if (out>0.0) outbyte |= 1<<((k%10)-1);	
      }

      resample_data_out.data_in[j]=out;
      
      k++;
    }

    if (soundfile_out) {
      resample_data_out.input_frames=resample_data.output_frames_gen;
      src_process(samplerate_out,&resample_data_out);

      i=sf_write_float(soundfile_out,
		       resample_data_out.data_out,
		       resample_data_out.output_frames_gen);

    }

  }

  sf_close(soundfile);
  if (soundfile_out) sf_close(soundfile_out);

  // restore serial port settings
  if (ttyfd>=0) {
    tcsetattr(ttyfd,TCSANOW,&oldtio);
  }

  return 0;
}
