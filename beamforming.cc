#include <cstring>
#include <memory>
#include <iostream>
#include <csignal>
#include <chrono>
#include <thread>
#include <respeaker.h>
#include <chain_nodes/pulse_collector_node.h>
#include <chain_nodes/vep_aec_beamforming_node.h>
#include <chain_nodes/snowboy_1b_doa_kws_node.h>

extern "C" {
#include <sndfile.h>
#include <unistd.h>
#include <getopt.h>
}

using namespace std;
using namespace respeaker;
#define BLOCK_SIZE_MS    8
static bool stop = false;

//How to stop the task
void SignalHandler(int signal){
  cerr << "Caught signal " << signal << ", terminating..." << endl;
  stop = true;
  //maintain the main thread untill the worker thread released its resource
  //std::this_thread::sleep_for(std::chrono::seconds(1));
}

int main(int argc, char *argv[]) {
    //Options
    int c;
    string source = "default";
    bool enable_agc = false;
    bool enable_wav = true;
    int agc_level = 10;
    string mic_type, kws;
    // Configures signal handling.
    struct sigaction sig_int_handler;
    sig_int_handler.sa_handler = SignalHandler;
    sigemptyset(&sig_int_handler.sa_mask);
    sig_int_handler.sa_flags = 0;
    sigaction(SIGINT, &sig_int_handler, NULL);
    sigaction(SIGTERM, &sig_int_handler, NULL);
    //Expose included classes
    unique_ptr<PulseCollectorNode> collector;
    unique_ptr<VepAecBeamformingNode> vep_1beam;
    unique_ptr<Snowboy1bDoaKwsNode> snowboy_kws;
    unique_ptr<ReSpeaker> respeaker;
    collector.reset(PulseCollectorNode::Create_48Kto16K(source, BLOCK_SIZE_MS));
    vep_1beam.reset(VepAecBeamformingNode::Create(StringToMicType(mic_type), true, 6, enable_wav));

    if (kws == "alexa") {
        cout << "Somethings broken" << endl; 
    } else {
        snowboy_kws.reset(Snowboy1bDoaKwsNode::Create("/usr/share/respeaker/snowboy/resources/common.res",
                                                    "/usr/share/respeaker/snowboy/resources//snowboy.umdl",
                                                    "0.5",
                                                    200,
                                                    enable_agc,
                                                    false));
    }

    //Print dir
    cout <<  respeaker->GetDirection(); << endl;
    //Should set dir for forming
    respeaker->SetDirection(30);

    //Handle volume
    if (enable_agc) {
        snowboy_kws->SetAgcTargetLevelDbfs(agc_level);
        cout << "AGC = -"<< agc_level<< endl;
    }
    else {
        cout << "Disable AGC" << endl;
    }  

    //Get from header refs
    vep_1beam->Uplink(collector.get());
    snowboy_kws->Uplink(vep_1beam.get());
    respeaker.reset(ReSpeaker::Create());
    respeaker->RegisterChainByHead(collector.get());
    respeaker->RegisterOutputNode(snowboy_kws.get());
    respeaker->RegisterDirectionManagerNode(snowboy_kws.get());
    respeaker->RegisterHotwordDetectionNode(snowboy_kws.get());  

    //Important
    if (!respeaker->Start(&stop)) {
        cout << "Can not start the respeaker node chain." << endl;
        return -1;
    }
    
    //Rest of it
    string data;
    int frames;
    size_t num_channels = respeaker->GetNumOutputChannels();
    int rate = respeaker->GetNumOutputRate();
    cout << "num channels: " << num_channels << ", rate: " << rate << endl;
    // init libsndfile
    SNDFILE *file ;
    SF_INFO sfinfo ;
    if (enable_wav) {
        memset (&sfinfo, 0, sizeof (sfinfo));
        sfinfo.samplerate   = rate ;
        sfinfo.channels     = num_channels ;
        sfinfo.format       = (SF_FORMAT_WAV | SF_FORMAT_PCM_16) ;
        if (! (file = sf_open ("audio_test001.wav", SFM_WRITE, &sfinfo)))
        {
            cout << sf_strerror(file) << endl;
            cout << "Error : Not able to open output file." << endl;
            return -1 ;
        }
    }
    int tick;
    int hotword_index = 0, hotword_count = 0;
    while (!stop)
    {
        data = respeaker->DetectHotword(hotword_index);
        if (hotword_index >= 1) {
            hotword_count++;
            cout << "hotword_count = " << hotword_count << endl;
        }
        if (enable_wav) {
            frames = data.length() / (sizeof(int16_t) * num_channels);
            sf_writef_short(file, (const int16_t *)(data.data()), frames);
        }
        if (tick++ % 5 == 0) {
            std::cout << "collector: " << collector->GetQueueDeepth() << ", vep_1beam: " <<
            vep_1beam->GetQueueDeepth() << ", snowboy_kws: " << snowboy_kws->GetQueueDeepth() <<
            std::endl;
        }
    }
    cout << "stopping the respeaker worker thread..." << endl;
    respeaker->Stop();
    cout << "cleanup done." << endl;
    if (enable_wav) {
        sf_close (file);
        cout << "wav file closed." << endl;
    }
    return 0;
}



}