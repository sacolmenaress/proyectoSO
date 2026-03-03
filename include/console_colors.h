#ifndef CONSOLE_COLORS_H
#define CONSOLE_COLORS_H

/* Códigos de escape ANSI para colores en terminal */
#define ANSI_RESET      "\x1b[0m"
#define ANSI_BOLD       "\x1b[1m"
#define ANSI_DIM        "\x1b[2m"

/* Colores de texto (Foreground) */
#define ANSI_FG_BLACK   "\x1b[30m"
#define ANSI_FG_RED     "\x1b[31m"
#define ANSI_FG_GREEN   "\x1b[32m"
#define ANSI_FG_YELLOW  "\x1b[33m"
#define ANSI_FG_BLUE    "\x1b[34m"
#define ANSI_FG_MAGENTA "\x1b[35m"
#define ANSI_FG_CYAN    "\x1b[36m"
#define ANSI_FG_WHITE   "\x1b[37m"

/* Colores de texto brillantes/negritas */
#define ANSI_FG_B_RED     "\x1b[1;31m"
#define ANSI_FG_B_GREEN   "\x1b[1;32m"
#define ANSI_FG_B_YELLOW  "\x1b[1;33m"
#define ANSI_FG_B_BLUE    "\x1b[1;34m"
#define ANSI_FG_B_MAGENTA "\x1b[1;35m"
#define ANSI_FG_B_CYAN    "\x1b[1;36m"
#define ANSI_FG_B_WHITE   "\x1b[1;37m"

/* Macros para limpiar pantalla */
#define CLEAR_SCREEN()  printf("\x1b[2J\x1b[H")

#endif /* CONSOLE_COLORS_H */
