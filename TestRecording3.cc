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
extern "C"
{
#include <sndfile.h>
#include <unistd.h>
#include <getopt.h>
}
using namespace std;
using namespace respeaker;
#define BLOCK_SIZE_MS    8
static bool stop = false;

void SignalHandler(int signal){
  cerr << "Caught signal " << signal << ", terminating..." << endl;
  stop = true;
  //maintain the main thread untill the worker thread released its resource
  //std::this_thread::sleep_for(std::chrono::seconds(1));
}
static void help(const char *argv0) {
    cout << "pulse_snowboy_1b_test [options]" << endl;
    cout << "A demo application for librespeaker." << endl << endl;
    cout << "  -h, --help                               Show this help" << endl;
    cout << "  -s, --source=SOURCE_NAME                 The source (microphone) to connect to" << endl;
    cout << "  -t, --type=MIC_TYPE                      The MICROPHONE TYPE, support: CIRCULAR_6MIC_7BEAM, LINEAR_6MIC_8BEAM, LINEAR_4MIC_1BEAM, CIRCULAR_4MIC_9BEAM" << endl;
    cout << "  -g, --agc=NEGTIVE INTEGER                The target gain level of output, [-31, 0]" << endl;
    cout << "  -w, --wav                                Enable output wav log, default is false." << endl;
    cout << "  -k, --kws=KWS_NAME                       The keyword name: snowboy or alexa, default is snowboy" << endl;
}
int main(int argc, char *argv[]) {
    // Configures signal handling.
    struct sigaction sig_int_handler;
    sig_int_handler.sa_handler = SignalHandler;
    sigemptyset(&sig_int_handler.sa_mask);
    sig_int_handler.sa_flags = 0;
    sigaction(SIGINT, &sig_int_handler, NULL);
    sigaction(SIGTERM, &sig_int_handler, NULL);
    // parse opts
    int c;
    string source = "default";
    bool enable_agc = false;
    bool enable_wav = true;
    int agc_level = 10;
    string kws;
    string mic_type = "CIRCULAR_6MIC_7BEAM";
    static const struct option long_options[] = {
        {"help",         0, NULL, 'h'},
        {"source",       1, NULL, 's'},
        {"kws",          1, NULL, 'k'},
        {"type",         1, NULL, 't'},
        {"agc",          1, NULL, 'g'},
        {"wav",          0, NULL, 'w'},
        {NULL,           0, NULL,  0}
    };
    while ((c = getopt_long(argc, argv, "k:t:g:s:hw", long_options, NULL)) != -1) {
        switch (c) {
        case 'h' :
            help(argv[0]);
            return 0;
        case 's':
            source = string(optarg);
            break;
        case 'k':
            kws = string(optarg);
            break;
        case 't':
            mic_type = string(optarg);
            break;
        case 'g':
            enable_agc = true;
            agc_level = stoi(optarg);
            if ((agc_level > 31) || (agc_level < -31)) agc_level = 31;
            if (agc_level < 0) agc_level = (0-agc_level);
            break;
        case 'w':
            enable_wav = true;
            break;
        default:
            return 0;
        }
    }
    unique_ptr<PulseCollectorNode> collector;
    unique_ptr<VepAecBeamformingNode> vep_1beam;
    unique_ptr<Snowboy1bDoaKwsNode> snowboy_kws;
    unique_ptr<ReSpeaker> respeaker;
    collector.reset(PulseCollectorNode::Create_48Kto16K(source, BLOCK_SIZE_MS));
    vep_1beam.reset(VepAecBeamformingNode::Create(StringToMicType(mic_type), true, 7, enable_wav));
    vep_1beam->SetAngleForMic0(30);
    if (kws == "alexa") {
        cout << "using alexa kws" << endl;
        snowboy_kws.reset(Snowboy1bDoaKwsNode::Create("/usr/share/respeaker/snowboy/resources/common.res",
                                                    // "/usr/share/respeaker/snowboy/resources/alexa.umdl",
                                                    "/usr/share/respeaker/snowboy/resources/alexa_02092017.umdl",
                                                    "0.5",
                                                    10,
                                                    enable_agc,
                                                    false));
    }
    else {
        cout << "using snowboy kws" << endl;
        snowboy_kws.reset(Snowboy1bDoaKwsNode::Create("/usr/share/respeaker/snowboy/resources/common.res",
                                                    "/usr/share/respeaker/snowboy/resources//snowboy.umdl",
                                                    "0.5",
                                                    10,
                                                    enable_agc,
                                                    false));
    }
    snowboy_kws->DisableAutoStateTransfer();
    if (enable_agc) {
        snowboy_kws->SetAgcTargetLevelDbfs(agc_level);
        cout << "AGC = -"<< agc_level<< endl;
    }
    else {
        cout << "Disable AGC" << endl;
    }  
    // collector->SetThreadPriority(50);
    // vep_1beam->SetThreadPriority(99);
    // snowboy_kws->SetThreadPriority(51);
    // vep_1beam->BindToCore(1);
    // snowboy_kws->BindToCore(2);
    vep_1beam->Uplink(collector.get());
    snowboy_kws->Uplink(vep_1beam.get());
    respeaker.reset(ReSpeaker::Create());
    respeaker->RegisterChainByHead(collector.get());
    respeaker->RegisterOutputNode(snowboy_kws.get());
    respeaker->RegisterDirectionManagerNode(snowboy_kws.get());
    respeaker->RegisterHotwordDetectionNode(snowboy_kws.get());  
    if (!respeaker->Start(&stop)) {
        cout << "Can not start the respeaker node chain." << endl;
        return -1;
    }
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
        if (! (file = sf_open ("audio_angletest.wav", SFM_WRITE, &sfinfo)))
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
