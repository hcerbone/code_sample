/*****************************************************************************/

//         ⊂_-
//       　  ＼＼
//        　  ＼( ͡° ͜ʖ ͡°)          welcome
//       　　    >　   ⌒ヽ
//       　     / 　 へ＼ \.
//       　　  /　　/   ＼＼                            to
//            ﾚ　ノ　　  ヽ_つ
//       　　 /　/
//       　  /　/|
//       　 (　(ヽ
//       　 |　|、＼         this bomb
//        　| 丿 ＼ ⌒)
//       　 | |　 ) /
//        ノ )　　Lﾉ
//        (_／

/*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "support.hh"
#include "phases.hh"

FILE* infile = stdin;

int main(int argc, char *argv[]) {
    // When run with no arguments, the bomb reads its input lines
    // from standard input.
    // When run with one argument <file>, the bomb reads from <file>
    // until EOF, and then switches to standard input. Thus, as you
    // defuse each phase, you can add its defusing string to <file> and
    // avoid having to retype it.
    if (argc == 2 && strcmp(argv[1], "-") != 0) {
        infile = fopen(argv[1], "r");
        if (!infile) {
            printf("%s: Error: Couldn't open %s\n", argv[0], argv[1]);
            exit(8);
        }
    } else if (argc > 2) {
        printf("Usage: %s [FILE]\n", argv[0]);
        exit(8);
    }

    // If you look at the code in this routine, you don't have to
    // understand any of the networking code, which starts after
    // the second function call in this routine.
    initialize_bomb();

    printf("THE UBIQUITOUS AI: "
           "Welcome to your twisted, high-tech near-future.\n");
    printf("Step carefully.\n");

    char* input = read_line();       // Get input
    phase_1(input);                  // Run the phase
    phase_defused();                 // Sends network message (you can ignore)
    printf("PHASE 1 DEFUSED.\n");

    // loops are for luzers
    input = read_line();
    phase_2(input);
    phase_defused();
    printf("PHASE 2 DEFUSED.\n");

    input = read_line();
    phase_3(input);
    phase_defused();
    printf("PHASE 3 DEFUSED.\n");

    input = read_line();
    phase_4(input);
    phase_defused();
    printf("PHASE 4 DEFUSED.\n");

    input = read_line();
    phase_5(input);
    phase_defused();
    printf("PHASE 5 DEFUSED.\n");

    input = read_line();
    phase_6(input);
    phase_defused();
    printf("PHASE 6 DEFUSED.\n");

    // You almost made it to the end credits. But there's often one
    // last scare! [Eerie Black Mirror music plays.]
    return 0;
}
