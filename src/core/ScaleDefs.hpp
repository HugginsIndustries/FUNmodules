#pragma once
#include <cstdint>
#include <vector>
/*
 * ScaleDefs.hpp — Single source of truth for built‑in scale tables used by PolyQuanta. 
 * ARRAY ORDER IS STABLE and MUST NOT CHANGE to preserve JSON/backwards compatibility 
 * (scaleIndex persisted values map directly to these indices). Any additions must 
 * append at the end only.
 */
namespace hi { namespace music {
struct Scale { 
    const char* name; 
    std::vector<uint8_t> mask; // vector of bits (bit0 = root degree)
    
    // Legacy constructors for backward compatibility
    Scale(const char* n, unsigned int bitmask, int edo) : name(n) {
        // Convert bitmask to vector format
        mask.resize(edo, 0);
        for (int i = 0; i < edo && i < 32; i++) {
            if (bitmask & (1u << i)) {
                mask[i] = 1;
            }
        }
    }
    
    Scale(const char* n, unsigned int bitmask) : name(n) {
        // Auto-detect EDO from bitmask (find highest set bit)
        int edo = 0;
        unsigned int temp = bitmask;
        while (temp) {
            temp >>= 1;
            edo++;
        }
        if (edo == 0) edo = 12; // default to 12-EDO
        
        mask.resize(edo, 0);
        for (int i = 0; i < edo && i < 32; i++) {
            if (bitmask & (1u << i)) {
                mask[i] = 1;
            }
        }
    }
    
    // Constructor for vector-based scales
    Scale(const char* n, const std::vector<uint8_t>& m) : name(n), mask(m) {}
};



// EDO-specific scales (1-120 in order)
extern const int NUM_SCALES_1EDO;
extern const int NUM_SCALES_2EDO;
extern const int NUM_SCALES_3EDO;
extern const int NUM_SCALES_4EDO;
extern const int NUM_SCALES_5EDO;
extern const int NUM_SCALES_6EDO;
extern const int NUM_SCALES_7EDO;
extern const int NUM_SCALES_8EDO;
extern const int NUM_SCALES_9EDO;
extern const int NUM_SCALES_10EDO;
extern const int NUM_SCALES_11EDO;
extern const int NUM_SCALES_12EDO;
extern const int NUM_SCALES_13EDO;
extern const int NUM_SCALES_14EDO;
extern const int NUM_SCALES_15EDO;
extern const int NUM_SCALES_16EDO;
extern const int NUM_SCALES_17EDO;
extern const int NUM_SCALES_18EDO;
extern const int NUM_SCALES_19EDO;
extern const int NUM_SCALES_20EDO;
extern const int NUM_SCALES_21EDO;
extern const int NUM_SCALES_22EDO;
extern const int NUM_SCALES_23EDO;
extern const int NUM_SCALES_24EDO;
extern const int NUM_SCALES_25EDO;
extern const int NUM_SCALES_26EDO;
extern const int NUM_SCALES_27EDO;
extern const int NUM_SCALES_28EDO;
extern const int NUM_SCALES_29EDO;
extern const int NUM_SCALES_30EDO;
extern const int NUM_SCALES_31EDO;
extern const int NUM_SCALES_32EDO;
extern const int NUM_SCALES_33EDO;
extern const int NUM_SCALES_34EDO;
extern const int NUM_SCALES_35EDO;
extern const int NUM_SCALES_36EDO;
extern const int NUM_SCALES_37EDO;
extern const int NUM_SCALES_38EDO;
extern const int NUM_SCALES_39EDO;
extern const int NUM_SCALES_40EDO;
extern const int NUM_SCALES_41EDO;
extern const int NUM_SCALES_42EDO;
extern const int NUM_SCALES_43EDO;
extern const int NUM_SCALES_44EDO;
extern const int NUM_SCALES_45EDO;
extern const int NUM_SCALES_46EDO;
extern const int NUM_SCALES_47EDO;
extern const int NUM_SCALES_48EDO;
extern const int NUM_SCALES_49EDO;
extern const int NUM_SCALES_50EDO;
extern const int NUM_SCALES_51EDO;
extern const int NUM_SCALES_52EDO;
extern const int NUM_SCALES_53EDO;
extern const int NUM_SCALES_54EDO;
extern const int NUM_SCALES_55EDO;
extern const int NUM_SCALES_56EDO;
extern const int NUM_SCALES_57EDO;
extern const int NUM_SCALES_58EDO;
extern const int NUM_SCALES_59EDO;
extern const int NUM_SCALES_60EDO;
extern const int NUM_SCALES_61EDO;
extern const int NUM_SCALES_62EDO;
extern const int NUM_SCALES_63EDO;
extern const int NUM_SCALES_64EDO;
extern const int NUM_SCALES_65EDO;
extern const int NUM_SCALES_66EDO;
extern const int NUM_SCALES_67EDO;
extern const int NUM_SCALES_68EDO;
extern const int NUM_SCALES_69EDO;
extern const int NUM_SCALES_70EDO;
extern const int NUM_SCALES_71EDO;
extern const int NUM_SCALES_72EDO;
extern const int NUM_SCALES_73EDO;
extern const int NUM_SCALES_74EDO;
extern const int NUM_SCALES_75EDO;
extern const int NUM_SCALES_76EDO;
extern const int NUM_SCALES_77EDO;
extern const int NUM_SCALES_78EDO;
extern const int NUM_SCALES_79EDO;
extern const int NUM_SCALES_80EDO;
extern const int NUM_SCALES_81EDO;
extern const int NUM_SCALES_82EDO;
extern const int NUM_SCALES_83EDO;
extern const int NUM_SCALES_84EDO;
extern const int NUM_SCALES_85EDO;
extern const int NUM_SCALES_86EDO;
extern const int NUM_SCALES_87EDO;
extern const int NUM_SCALES_88EDO;
extern const int NUM_SCALES_89EDO;
extern const int NUM_SCALES_90EDO;
extern const int NUM_SCALES_91EDO;
extern const int NUM_SCALES_92EDO;
extern const int NUM_SCALES_93EDO;
extern const int NUM_SCALES_94EDO;
extern const int NUM_SCALES_95EDO;
extern const int NUM_SCALES_96EDO;
extern const int NUM_SCALES_97EDO;
extern const int NUM_SCALES_98EDO;
extern const int NUM_SCALES_99EDO;
extern const int NUM_SCALES_100EDO;
extern const int NUM_SCALES_101EDO;
extern const int NUM_SCALES_102EDO;
extern const int NUM_SCALES_103EDO;
extern const int NUM_SCALES_104EDO;
extern const int NUM_SCALES_105EDO;
extern const int NUM_SCALES_106EDO;
extern const int NUM_SCALES_107EDO;
extern const int NUM_SCALES_108EDO;
extern const int NUM_SCALES_109EDO;
extern const int NUM_SCALES_110EDO;
extern const int NUM_SCALES_111EDO;
extern const int NUM_SCALES_112EDO;
extern const int NUM_SCALES_113EDO;
extern const int NUM_SCALES_114EDO;
extern const int NUM_SCALES_115EDO;
extern const int NUM_SCALES_116EDO;
extern const int NUM_SCALES_117EDO;
extern const int NUM_SCALES_118EDO;
extern const int NUM_SCALES_119EDO;
extern const int NUM_SCALES_120EDO;

// Accessor functions for EDO scales (1-120 in order)
const Scale* scales1EDO();
const Scale* scales2EDO();
const Scale* scales3EDO();
const Scale* scales4EDO();
const Scale* scales5EDO();
const Scale* scales6EDO();
const Scale* scales7EDO();
const Scale* scales8EDO();
const Scale* scales9EDO();
const Scale* scales10EDO();
const Scale* scales11EDO();
const Scale* scales12EDO();
const Scale* scales13EDO();
const Scale* scales14EDO();
const Scale* scales15EDO();
const Scale* scales16EDO();
const Scale* scales17EDO();
const Scale* scales18EDO();
const Scale* scales19EDO();
const Scale* scales20EDO();
const Scale* scales21EDO();
const Scale* scales22EDO();
const Scale* scales23EDO();
const Scale* scales24EDO();
const Scale* scales25EDO();
const Scale* scales26EDO();
const Scale* scales27EDO();
const Scale* scales28EDO();
const Scale* scales29EDO();
const Scale* scales30EDO();
const Scale* scales31EDO();
const Scale* scales32EDO();
const Scale* scales33EDO();
const Scale* scales34EDO();
const Scale* scales35EDO();
const Scale* scales36EDO();
const Scale* scales37EDO();
const Scale* scales38EDO();
const Scale* scales39EDO();
const Scale* scales40EDO();
const Scale* scales41EDO();
const Scale* scales42EDO();
const Scale* scales43EDO();
const Scale* scales44EDO();
const Scale* scales45EDO();
const Scale* scales46EDO();
const Scale* scales47EDO();
const Scale* scales48EDO();
const Scale* scales49EDO();
const Scale* scales50EDO();
const Scale* scales51EDO();
const Scale* scales52EDO();
const Scale* scales53EDO();
const Scale* scales54EDO();
const Scale* scales55EDO();
const Scale* scales56EDO();
const Scale* scales57EDO();
const Scale* scales58EDO();
const Scale* scales59EDO();
const Scale* scales60EDO();
const Scale* scales61EDO();
const Scale* scales62EDO();
const Scale* scales63EDO();
const Scale* scales64EDO();
const Scale* scales65EDO();
const Scale* scales66EDO();
const Scale* scales67EDO();
const Scale* scales68EDO();
const Scale* scales69EDO();
const Scale* scales70EDO();
const Scale* scales71EDO();
const Scale* scales72EDO();
const Scale* scales73EDO();
const Scale* scales74EDO();
const Scale* scales75EDO();
const Scale* scales76EDO();
const Scale* scales77EDO();
const Scale* scales78EDO();
const Scale* scales79EDO();
const Scale* scales80EDO();
const Scale* scales81EDO();
const Scale* scales82EDO();
const Scale* scales83EDO();
const Scale* scales84EDO();
const Scale* scales85EDO();
const Scale* scales86EDO();
const Scale* scales87EDO();
const Scale* scales88EDO();
const Scale* scales89EDO();
const Scale* scales90EDO();
const Scale* scales91EDO();
const Scale* scales92EDO();
const Scale* scales93EDO();
const Scale* scales94EDO();
const Scale* scales95EDO();
const Scale* scales96EDO();
const Scale* scales97EDO();
const Scale* scales98EDO();
const Scale* scales99EDO();
const Scale* scales100EDO();
const Scale* scales101EDO();
const Scale* scales102EDO();
const Scale* scales103EDO();
const Scale* scales104EDO();
const Scale* scales105EDO();
const Scale* scales106EDO();
const Scale* scales107EDO();
const Scale* scales108EDO();
const Scale* scales109EDO();
const Scale* scales110EDO();
const Scale* scales111EDO();
const Scale* scales112EDO();
const Scale* scales113EDO();
const Scale* scales114EDO();
const Scale* scales115EDO();
const Scale* scales116EDO();
const Scale* scales117EDO();
const Scale* scales118EDO();
const Scale* scales119EDO();
const Scale* scales120EDO();

// Helper function to get EDO scales based on Number of EDO divisions
const Scale* scalesEDO(int edo);
int numScalesEDO(int edo);
}} // namespace hi::music
