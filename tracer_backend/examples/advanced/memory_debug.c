/*
 * memory_debug.c - Memory diagnostics ADA tracing example
 *
 * Exercises common defect patterns so tooling can highlight leaks and misuse.
 * The program intentionally leaks memory, dereferences freed pointers, and
 * leaves a linked list uncollected. A double free helper is included but kept
 * disabled to avoid crashing the demo.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct node {
    int value;
    struct node *next;
};

static void leaky_function(void) {
    char *message = (char *)malloc(128);
    if (message == NULL) {
        fputs("Allocation failed in leaky_function.\n", stderr);
        exit(EXIT_FAILURE);
    }

    strcpy(message, "This buffer is never freed – trace tools should flag it.");
    printf("leaky_function: %s\n", message);
    /* Intentionally omit free(message); */
}

static void use_after_free(void) {
    int *number = (int *)malloc(sizeof(int));
    if (number == NULL) {
        fputs("Allocation failed in use_after_free.\n", stderr);
        exit(EXIT_FAILURE);
    }

    *number = 42;
    free(number);

    /* Intentional mistake: accessing memory after it has been freed. */
    printf("use_after_free: stale value still reads as %d (undefined behaviour).\n", *number);
}

/*
static void double_free(void) {
    char *payload = (char *)malloc(32);
    if (payload == NULL) {
        fputs("Allocation failed in double_free.\n", stderr);
        exit(EXIT_FAILURE);
    }
    strcpy(payload, "double free crash");
    free(payload);
    free(payload); // Intentional double free – disabled to keep example stable.
}
*/

static void leak_linked_list(void) {
    struct node *head = NULL;
    struct node *tail = NULL;

    for (int i = 0; i < 5; ++i) {
        struct node *current = (struct node *)malloc(sizeof(struct node));
        if (current == NULL) {
            fputs("Allocation failed while building linked list.\n", stderr);
            exit(EXIT_FAILURE);
        }
        current->value = i;
        current->next = NULL;

        if (head == NULL) {
            head = current;
        } else {
            tail->next = current;
        }
        tail = current;
    }

    printf("leak_linked_list: constructed linked list starting at %p and never freed.\n", (void *)head);
    /* The list is intentionally leaked to demonstrate heap growth in traces. */
}

int main(void) {
    puts("Memory debugging example: provoking diagnostic-friendly mistakes.");

    leaky_function();
    use_after_free();
    leak_linked_list();

    puts("double_free helper remains commented out to keep the example running.");
    puts("Run with sanitizers or tracing to inspect the reported issues.");

    return 0;
}
