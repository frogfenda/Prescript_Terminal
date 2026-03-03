#include "prescript_data.h"

static const char* prescripts[] = {
     "FIND ISHMAEL.",
    "KILL YOUR PAINTING.",
    "CLEAR.",
    "I'M BURGER.",
    "STARE AT A CORNER FOR EXACTLY 300 SECONDS.",    
    "TEAR A BLANK PAGE INTO EXACTLY 17 PIECES.",     
    "WHISPER 'YES' TO THE NEXT PERSON WHO SPEAKS.",  
    "LEAVE A RUSTY COIN ON THE THIRD STAIR.",        
    "DO NOT BLINK WHEN YOU CROSS THE NEXT DOOR.",    

    "To... the one who has just opened their eyes, eat yesterday's leftover soup while standing at the kitchen counter. Do not heat it.",
    "To... the person wearing a red tie today, at precisely 3:33 PM, pour a glass of water onto the soil of the nearest potted plant. Say nothing.",
    "To... the third customer to enter the bakery on Elm Street, purchase seven loaves of bread. Consume the first slice of the seventh loaf before exiting. Dispose of the remainder in the blue dumpster in the alley.",
    "To... the one reading this note, find the oldest book on your shelf. Tear out page 22 and fold it into a paper boat. Set it afloat in the nearest public fountain before sunset.",
    "To... the first person you make eye contact with after receiving this, ask them for the time. Regardless of their answer, respond with: 'The time is always correct for the needle.'",
    "To... the bearer, do not speak for the next hour. Communicate only through gestures. If you fail, cut a lock of your own hair.",
    "To... the resident of apartment 4B, tonight, place a single lit candle on your windowsill facing north. Let it burn out completely.",
    "To... the one with ink-stained fingers, write the word 'OBEDIENCE' one hundred times on a sheet of paper. Burn the paper upon completion. Inhale the smoke.",
    "To... the commuter on the 8:15 train, offer your seat to the tallest individual in the carriage. Remain standing for the duration of your journey.",
    "To... the one who feels doubt, walk to the nearest crossroads. Spin three times counterclockwise. Proceed in the direction you are facing for exactly 100 paces.",
    "To... the listener, the next song you hear, memorize its final lyric. Whisper it to the first stray animal you encounter.",
    "To... the cook, prepare a meal using only ingredients that are blue or purple. You must finish it entirely alone.",
    "To... the dreamer, stay awake until the clock's hour and minute hands overlap. At that moment, prick your finger and mark this note with a drop of blood.",
    "To... the witness, find a mirror in a public restroom. Look into your own eyes for two full minutes without blinking. If you blink, break the mirror.",
    "To... the holder of a copper coin, flip it at a bus stop. If it lands heads, board the next bus. If tails, walk in the opposite direction of its route for 30 minutes.",
    "To... the one with a silent phone, call the last number in your call history. Do not speak. Listen for 30 seconds, then hang up regardless.",
    "To... the wearer of black shoes, at noon, stand on the highest elevation in your current district. Face east and remain perfectly still for 180 seconds.",
    "To... the one who receives this via terminal, disconnect from the network for the next 24 hours. If you reconnect prematurely, format your primary drive.",
    "To... the first-born, find a photograph of your younger self. Hold it over a flame until the edges curl. Keep the ashes in your pocket for the rest of the day.",
    "To... the one seeking warmth, extinguish all artificial heat sources in your dwelling for the next night. Use only a single blanket.",
    "To... the one who speaks too much, thread a needle with red thread. Sew your lips shut metaphorically by piercing the collar of your garment seven times while silent.",
    "To... the observer, find a window with a view of a street. Count the number of people wearing hats. If the number is even, smile. If odd, frown. Maintain the expression for one hour.",
    "To... the keeper of secrets, write your greatest fear on a slip of paper. Fold it, place it under a rock in a nearby park. Never return to that rock.",
    "To... the one ascending stairs, skip every third step. If you miss your count, return to the bottom and begin again.",
    "To... the person near flowing water, drop a valuable possession into the current. Do not retrieve it."
};

static const int PRESCRIPT_COUNT = sizeof(prescripts) / sizeof(prescripts[0]);

int Get_Prescript_Count() { return PRESCRIPT_COUNT; }
const char* Get_Prescript(int index) { return prescripts[index]; }