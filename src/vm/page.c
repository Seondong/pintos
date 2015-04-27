#include "vm/page.h"
#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <hash.h>
#include "threads/malloc.h"

/* Supplemental page table. */
static struct hash page_table;

static hash_hash_func page_hash;
static hash_less_func page_less;
static hash_action_func page_destructor;

/* Initializes the supplemental page table. */
bool
page_init (void)
{
  return hash_init (&page_table, page_hash, page_less, NULL);
}

/* Inserts a page with given ADDRESS into the supplemental page
   table.  If there is no such page, returns NULL. */
struct page *
page_insert (const void *address)
{
  struct page *p = (struct page *) malloc (sizeof (struct page));
  struct hash_elem *e;

  p->addr = (void *) address;
  e = hash_insert (&page_table, &p->hash_elem);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

/* Finds a page with the given ADDRESS from the page table. */
struct page *
page_find (const void *address)
{
  struct page p;
  struct hash_elem *e;

  p.addr = (void *) address;
  e = hash_find (&page_table, &p.hash_elem);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

/* Clears the page table. */
void
page_clear (void)
{
  hash_clear (&page_table, page_destructor);
}

/* Returns a hash value for page P. */
static unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->addr, sizeof p->addr);
}

/* Returns true if page A precedes page B. */
static bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);

  return a->addr < b->addr;
}

/* Free a page. */
static void
page_destructor (struct hash_elem *e, void *aux UNUSED)
{
  free (hash_entry (e, struct page, hash_elem));
}
