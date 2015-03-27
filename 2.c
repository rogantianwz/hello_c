#include <stdio.h>
#include <string.h>

#define OK       0
#define NO_INPUT 1
#define TOO_LONG 2

static int getLine(char*, char*, size_t);

int main() {
    int rc;
    char buff[10];

    rc = getLine ("please input your name > ", buff, sizeof(buff));
    if (rc == NO_INPUT) {
        // Extra NL since my system doesn't output that on EOF.
        printf ("\nNo input\n");
        return 1;
    }

    if (rc == TOO_LONG) {
        printf ("Input too long [%s]\n", buff);
        return 1;
    }

    printf ("Hello [%s]\n", buff);

    return 0;
}



/**
 *fgets(...)Reads at most count - 1 characters from the given file stream and stores them in str. The produced character string is always null-terminated. Parsing stops if end-of-file occurs or a newline character is found, in which case str will contain that newline character.
 *
 *
 * reference: http://stackoverflow.com/questions/4023895/how-to-read-string-entered-by-user-in-c  
 * if you scanf("%s") into a 20 byte buffer and the user enters a 40-byte line, you're hosed. The whole point of scanf is scan-formatted and there is little more unformatted than user input
 *
 * scanf from the console isn't always bad, it can be useful for limited things like numeric input (and homework assignments) and such. But even then, it's not as robust as it should be for a production-quality application. Even when I'm needing to parse the input with a scanf-like operation, I'll fgets it into a buffer then sscanf it from there
 *
 * there is another way to resolve the problem ,see ./ref/ggets.c
 */
int getLine (char *prmpt, char *buff, size_t sz) {
    int ch, extra;

    // Get line with buffer overrun protection.
    if (prmpt != NULL) {
        printf ("%s", prmpt);
        fflush (stdout);
    }
    if (fgets (buff, sz, stdin) == NULL)
        return NO_INPUT;

    // If it was too long, there'll be no newline. In that case, we flush
    // to end of line so that excess doesn't affect the next call.
    if (buff[strlen(buff)-1] != '\n') {
        extra = 0;
        while (((ch = getchar()) != '\n') && (ch != EOF))
            extra = 1;
        return (extra == 1) ? TOO_LONG : OK;
    }

    // Otherwise remove newline and give string back to caller.
    buff[strlen(buff)-1] = '\0';
    return OK;
}
