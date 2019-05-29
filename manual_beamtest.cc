#include <cstring>
#include <memory>
#include <iostream>
#include <csignal>
#include <chrono>
#include <thread>

#include <respeaker.h>
#include <chain_nodes/pulse_collector_node.h>
#include <chain_nodes/vep_aec_beamforming_node.h>
#include <chain_nodes/snowboy_manual_beam_kws_node.h>

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
    cout << "pulse_snowboy_manual_beam_test [options]" << endl;
    cout << "A demo application for librespeaker." << endl << endl;
    cout << "  -h, --help                               Show this help" << endl;
    cout << "  -s, --source=SOURCE_NAME                 The source (microphone) to connect to" << endl;
    cout << "  -k, --kws                                The keyword name: snowboy or alexa, or nokws(which means no kws), default is snowboy" << endl;
    cout << "  -t, --type=MIC_TYPE                      The MICROPHONE TYPE, support: CIRCULAR_6MIC_7BEAM, LINEAR_6MIC_8BEAM, LINEAR_4MIC_1BEAM, CIRCULAR_4MIC_9BEAM" << endl;
    cout << "  -g, --agc=NEGTIVE INTEGER                The target gain level of output, [-31, 0]" << endl;
    cout << "  -w, --wav                                Enable output wav log, default is false." << endl;
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
    bool enable_kws = false;
    bool enable_wav = false;
    bool enable_agc = false;
    int agc_level = 10;
    string kws, mic_type;

    static const struct option long_options[] = {
        {"help",         0, NULL, 'h'},
        {"source",       1, NULL, 's'},
        {"kws",          1, NULL, 'k'},
        {"type",         1, NULL, 't'},
        {"agc",          1, NULL, 'g'},
        {"wav",          0, NULL, 'w'},
        {NULL,           0, NULL,  0}
    };

    while ((c = getopt_long(argc, argv, "s:k:t:g:hw", long_options, NULL)) != -1) {

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
    unique_ptr<VepAecBeamformingNode> vep_bf;
    unique_ptr<SnowboyManKwsNode> man_kws;
    unique_ptr<ReSpeaker> respeaker;

    collector.reset(PulseCollectorNode::Create_48Kto16K(source, BLOCK_SIZE_MS));
    vep_bf.reset(VepAecBeamformingNode::Create(StringToMicType(mic_type), false, 6, enable_wav));

    // static SnowboyManKwsNode* Create(std::string snowboy_resource_path,
    //                     std::string snowboy_model_path,
    //                     std::string snowboy_sensitivity,
    //                     int underclocking_count,
    //                     bool enable_agc,
    //                     bool enable_kws,
    //                     bool output_interleaved=false);
    if (kws == "nokws") {
        cout << "Disable kws" << endl;
        man_kws.reset(SnowboyManKwsNode::Create("/usr/share/respeaker/snowboy/resources/common.res",
                                        "/usr/share/respeaker/snowboy/resources//snowboy.umdl",
                                        "0.5",
                                        10,
                                        enable_agc,
                                        false,
                                        false));
    }
    else if (kws == "alexa") {
        cout << "using alexa kws" << endl;
        man_kws.reset(SnowboyManKwsNode::Create("/usr/share/respeaker/snowboy/resources/common.res",
                                                // "/usr/share/respeaker/snowboy/resources/alexa.umdl",
                                                "/usr/share/respeaker/snowboy/resources/alexa_02092017.umdl",
                                                "0.5",
                                                10,
                                                enable_agc,
                                                true,
                                                false));
    }
    else {
        cout << "using snowboy kws" << endl;
        man_kws.reset(SnowboyManKwsNode::Create("/usr/share/respeaker/snowboy/resources/common.res",
                                        "/usr/share/respeaker/snowboy/resources//snowboy.umdl",
                                        "0.5",
                                        10,
                                        enable_agc,
                                        true,
                                        false));
    }
