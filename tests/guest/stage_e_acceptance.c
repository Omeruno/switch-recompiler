/* Production Win32 regressions in one process; no host scripting or button input.
   This is a software gate, not a claim of GPU/scanout or Paint acceptance. */
#include <windows.h>
#include <stdio.h>
#ifndef SR_ACCEPTANCE_BUILD
#define SR_ACCEPTANCE_BUILD "unknown"
#endif
int StageE_stage_c(void);
int StageE_presentation(void);
int StageE_composition(void);
int StageE_dialog(int,char**);
int StageE_color(int,char**);
int StageE_comdlg(void);
static int dialogs(void) {char* argv[]={"stage_e_acceptance.exe",NULL};return StageE_dialog(1,argv);}
static int colors(void) {char* argv[]={"stage_e_acceptance.exe",NULL};return StageE_color(1,argv);}
struct Group {const char* name;int (*run)(void);};
static const struct Group groups[]={
    {"Stage C: all eight runtime suites",StageE_stage_c},
    {"common file dialog API validation",StageE_comdlg},
    {"standard dialog subclass and callback replacement",dialogs},
    {"ChooseColor A/W, hooks, cancellation and owner restore",colors},
    {"desktop geometry and CPU GDI corners",StageE_presentation},
    {"window composition: all 28 scenes",StageE_composition}
};
int main(void) {
    FILE* report=fopen("stage-e-acceptance.txt","w");unsigned i;
    if(!report){puts("FAIL: cannot create stage-e-acceptance.txt");return 2;}
    fprintf(report,"Stage E combined software acceptance v1\nFixture build: %s\nDesktop: %dx%d\nRUNNING: incomplete report is not a pass\n",
        SR_ACCEPTANCE_BUILD,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN));
    fflush(report);
    for(i=0;i<sizeof(groups)/sizeof(groups[0]);++i) {
        int code;
        fprintf(report,"BEGIN %u: %s\n",i+1,groups[i].name);fflush(report);
        code=groups[i].run();
        if(code) {
            fprintf(report,"FAIL %u: code %d; see component report\n",i+1,code);
            fclose(report);return code;
        }
        fprintf(report,"PASS %u: %s\n",i+1,groups[i].name);fflush(report);
    }
    fprintf(report,"PASS: ALL 6 SOFTWARE GROUPS\nPENDING: GPU output equivalence on Switch, software fallback, Paint dialogs/open/save, measured performance and physical input latency\n");
    fclose(report);
    puts("PASSED: Stage E combined acceptance, ALL 6 SOFTWARE GROUPS; hardware gates remain open");
    return 0;
}
