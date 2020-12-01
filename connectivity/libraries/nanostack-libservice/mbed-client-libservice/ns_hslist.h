/*
 * Copyright (c) 2014-2020 ARM Limited. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef NS_HSLIST_H_
#define NS_HSLIST_H_

#include "ns_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \defgroup ns_hslist Linked list support library.
 *
 * The ns_hslist.h file provides a singly-linked list providing O(1)
 * performance for most provided operations.
 *
 * See \ref ns_hslist.h for documentation.
 */

/** \file
 * \ingroup ns_hslist
 * \brief Linked list support library.
 *
 * The ns_hslist.h file provides a singly-linked list providing O(1)
 * performance for most provided operations.
 *
 * Memory footprint is one pointer for the list head, and one pointer in each
 * list entry. It is similar in concept to BSD's SLIST.
 *
 * * O(1) operations:
 * * * get_first, get_next
 * * * add_to_start, add_after
 * * * remove_first, remove_next
 * * O(n) operations - use ns_list if these are frequently used:
 * * * remove
 * * Unsupported operations - use ns_list or ns_slist if these are required:
 * * * get_last, get_previous, foreach_reverse
 * * * add_before, add_to_end
 * * * replace
 * * * concatenate
 *
 * Example of an entry type that can be stored to this list.
 * ~~~
 *     typedef struct example_entry
 *     {
 *         uint8_t          *data;
 *         uint32_t         data_count;
 *         ns_hslist_link_t link;
 *     }
 *     example_entry_t;
 *
 *     static NS_HSLIST_HEAD(example_entry_t, link) my_list;
 *     ns_hslist_init(&my_list);
 * ~~~
 * OR
 * ~~~
 *     NS_HSLIST_HEAD(example_entry_t, link) my_list = NS_HSLIST_INIT(my_list);
 * ~~~
 * OR
 * ~~~
 *     static NS_HSLIST_DEFINE(my_list, example_entry_t, link);
 * ~~~
 * OR
 * ~~~
 *     typedef NS_HSLIST_HEAD(example_entry_t, link) example_list_t;
 *     example_list_t NS_HSLIST_NAME_INIT(my_list);
 * ~~~
 * NOTE: the link field SHALL NOT be accessed by the user.
 *
 * An entry can exist on multiple lists by having multiple link fields.
 *
 * All the list operations are implemented as macros, most of which are backed
 * by optionally-inline functions. The macros do not evaluate any arguments more
 * than once, unless documented.
 *
 * In macro documentation, `list_t` refers to a list type defined using
 * NS_HSLIST_HEAD(), and `entry_t` to the entry type that was passed to it.
 */

/** \brief Underlying generic linked list head.
 *
 * Users should not use this type directly, but use the NS_HSLIST_HEAD() macro.
 */
typedef struct ns_hslist {
    void *first_entry;      ///< Pointer to first entry, or NULL if list is empty
} ns_hslist_t;

/** \brief Declare a list head type
 *
 * This union stores the real list head, and also encodes as compile-time type
 * information the offset of the link pointer, and the type of the entry.
 *
 * Note that type information is compiler-dependent; this means
 * ns_hslist_get_first() could return either `void *`, or a pointer to the actual
 * entry type. So `ns_hslist_get_first()->data` is not a portable construct -
 * always assign returned entry pointers to a properly typed pointer variable.
 * This assignment will be then type-checked where the compiler supports it, and
 * will dereference correctly on compilers that don't support this extension.
 *
 * If you need to support C++03 compilers that cannot return properly-typed
 * pointers, such as IAR 7, you need to use NS_HSLIST_TYPECOERCE to force the type.
 * ~~~
 *     NS_HSLIST_HEAD(example_entry_t, link) my_list;
 *
 *     example_entry_t *entry = ns_hslist_get_first(&my_list);
 *     do_something(entry->data);
 * ~~~
 * Each use of this macro generates a new anonymous union, so these two lists
 * have different types:
 * ~~~
 *     NS_HSLIST_HEAD(example_entry_t, link) my_list1;
 *     NS_HSLIST_HEAD(example_entry_t, link) my_list2;
 * ~~~
 * If you need to use a list type in multiple places, eg as a function
 * parameter, use typedef:
 * ~~~
 *     typedef NS_HSLIST_HEAD(example_entry_t, link) example_list_t;
 *
 *     void example_function(example_list_t *);
 * ~~~
 */
#define NS_HSLIST_HEAD(entry_type, field) \
    NS_HSLIST_HEAD_BY_OFFSET_(entry_type, offsetof(entry_type, field))

/** \brief Declare a list head type for an incomplete entry type.
 *
 * This declares a list head, similarly to NS_HSLIST_HEAD(), but unlike that
 * this can be used in contexts where the entry type may be incomplete.
 *
 * To use this, the link pointer must be the first member in the
 * actual complete structure. This is NOT checked - the definition of the
 * element should probably test NS_STATIC_ASSERT(offsetof(type, link) == 0)
 * if outside users are known to be using NS_HSLIST_HEAD_INCOMPLETE().
 * ~~~
 *     struct opaque;
 *     NS_HSLIST_HEAD_INCOMPLETE(struct opaque) opaque_list;
 * ~~~
 */
#define NS_HSLIST_HEAD_INCOMPLETE(entry_type) \
    NS_HSLIST_HEAD_BY_OFFSET_(entry_type, 0)

/// \privatesection
/** \brief Internal macro defining a list head, given the offset to the link pointer
 * The +1 allows for link_offset being 0 - we can't declare a 0-size array
 */
#define NS_HSLIST_HEAD_BY_OFFSET_(entry_type, link_offset) \
union \
{ \
    ns_hslist_t slist; \
    NS_FUNNY_COMPARE_OK \
    NS_STATIC_ASSERT(link_offset <= (ns_hslist_offset_t) -1, "link offset too large") \
    NS_FUNNY_COMPARE_RESTORE \
    char (*offset)[link_offset + 1]; \
    entry_type *type; \
}

/** \brief Get offset of link field in entry.
 * \return `(ns_hslist_offset_t)` The offset of the link field for entries on the specified list
 */
#define NS_HSLIST_OFFSET_(list) ((ns_hslist_offset_t) (sizeof *(list)->offset - 1))

/** \brief Get the entry pointer type.
 * \def NS_HSLIST_PTR_TYPE_
 *
 * \return An unqualified pointer type to an entry on the specified list.
 *
 * Only available if the compiler provides a "typeof" operator.
 */
#if defined __cplusplus && __cplusplus >= 201103L
#define NS_HSLIST_PTR_TYPE_(list) decltype((list)->type)
#elif defined __GNUC__
#define NS_HSLIST_PTR_TYPE_(list) __typeof__((list)->type)
#endif

/** \brief Check for compatible pointer types
 *
 * This test will produce a diagnostic about a pointer mismatch on
 * the == inside the sizeof operator. For example ARM/Norcroft C gives the error:
 *
 *     operand types are incompatible ("entry_t *" and "other_t *")
 */
#ifdef CPPCHECK
#define NS_HSLIST_PTR_MATCH_(a, b, str) ((void) 0)
#else
#define NS_HSLIST_PTR_MATCH_(a, b, str) ((void) sizeof ((a) == (b)))
#endif

/** \brief Internal macro to cast returned entry pointers to correct type.
 *
 * Not portable in C, alas. With GCC or C++11, the "get entry" macros return
 * correctly-typed pointers. Otherwise, the macros return `void *`.
 *
 * The attempt at a portable version would work if the C `?:` operator wasn't
 * broken - `x ? (t *) : (void *)` should really have type `(t *)` in C, but
 * it has type `(void *)`, which only makes sense for C++. The `?:` is left in,
 * in case some day it works. Some compilers may still warn if this is
 * assigned to a different type.
 */
#ifdef NS_HSLIST_PTR_TYPE_
#define NS_HSLIST_TYPECAST_(list, val) ((NS_HSLIST_PTR_TYPE_(list)) (val))
#else
#define NS_HSLIST_TYPECAST_(list, val) (0 ? (list)->type : (val))
#endif

/** \brief Macro to force correct type if necessary.
 *
 * In C, doesn't matter if NS_HSLIST_TYPECAST_ works or not, as it's legal
 * to assign void * to a pointer. In C++, we can't do that, so need
 * a back-up plan for C++03. This forces the type, so breaks type-safety -
 * only activate when needed, meaning we still get typechecks on other
 * toolchains.
 *
 * If a straight assignment of a ns_hslist function to a pointer fails
 * on a C++03 compiler, use the following construct. This will not be
 * required with C++11 compilers.
 * ~~~
 *     type *elem = NS_HSLIST_TYPECOERCE(type *, ns_hslist_get_first(list));
 * ~~~
 */
#if defined(NS_HSLIST_PTR_TYPE_) || !defined(__cplusplus)
#define NS_HSLIST_TYPECOERCE(type, val) (val)
#else
#define NS_HSLIST_TYPECOERCE(type, val) (type) (val)
#endif

/** \brief Internal macro to check types of input entry pointer. */
#define NS_HSLIST_TYPECHECK_(list, entry) \
    (NS_HSLIST_PTR_MATCH_((list)->type, (entry), "incorrect entry type for list"), (entry))

/** \brief Type used to pass link offset to underlying functions
 *
 * We could use size_t, but it would be unnecessarily large on 8-bit systems,
 * where we can be (pretty) confident we won't have next pointers more than
 * 256 bytes into a structure.
 */
typedef uint_fast8_t ns_hslist_offset_t;

/// \publicsection
/** \brief The type for the link member in the user's entry structure.
 *
 * Users should not access this member directly - just pass its name to the
 * list head macros.
 *
 * NB - the list implementation relies on next being the first member.
 */
typedef struct ns_hslist_link {
    void *next;     ///< Pointer to next entry, or NULL if none
} ns_hslist_link_t;

/** \brief "Poison" value placed in unattached entries' link pointers.
 * \internal What are good values for this? Platform dependent, maybe just NULL
 */
#define NS_HSLIST_POISON ((void *) 0xDEADBEEF)

/** \brief Initialiser for an entry's link member
 *
 * This initialiser is not required by the library, but a user may want an
 * initialiser to include in their own entry initialiser. See
 * ns_hslist_link_init() for more discussion.
 */
#define NS_HSLIST_LINK_INIT(name) \
    NS_FUNNY_INTPTR_OK \
    { NS_HSLIST_POISON } \
    NS_FUNNY_INTPTR_RESTORE

/** \hideinitializer \brief Initialise an entry's list link
 *
 * This "initialises" an unattached entry's link by filling the fields with
 * poison. This is optional, as unattached entries field pointers are not
 * meaningful, and it is not valid to call ns_hslist_get_next or similar on
 * an unattached entry.
 *
 * \param entry Pointer to an entry
 * \param field The name of the link member to initialise
 */
#define ns_hslist_link_init(entry, field) ns_hslist_link_init_(&(entry)->field)

/** \hideinitializer \brief Initialise a list
 *
 * Initialise a list head before use. A list head must be initialised using this
 * function or one of the NS_HSLIST_INIT()-type macros before use. A zero-initialised
 * list head is *not* valid.
 *
 * If used on a list containing existing entries, those entries will
 * become detached. (They are not modified, but their links are now effectively
 * undefined).
 *
 * \param list Pointer to a NS_HSLIST_HEAD() structure.
 */
#define ns_hslist_init(list) ns_hslist_init_(&(list)->slist)

/** \brief Initialiser for an empty list
 *
 * Usage in an enclosing initialiser:
 * ~~~
 *      static my_type_including_list_t x = {
 *          "Something",
 *          23,
 *          NS_HSLIST_INIT(x),
 *      };
 * ~~~
 * NS_HSLIST_DEFINE() or NS_HSLIST_NAME_INIT() may provide a shorter alternative
 * in simpler cases.
 */
#define NS_HSLIST_INIT(name) { { NULL } }

/** \brief Name and initialiser for an empty list
 *
 * Usage:
 * ~~~
 *      list_t NS_HSLIST_NAME_INIT(foo);
 * ~~~
 * acts as
 * ~~~
 *      list_t foo = { empty list };
 * ~~~
 * Also useful with designated initialisers:
 * ~~~
 *      .NS_HSLIST_NAME_INIT(foo),
 * ~~~
 * acts as
 * ~~~
 *      .foo = { empty list },
 * ~~~
 */
#define NS_HSLIST_NAME_INIT(name) name = NS_HSLIST_INIT(name)

/** \brief Define a list, and initialise to empty.
 *
 * Usage:
 * ~~~
 *     static NS_HSLIST_DEFINE(my_list, entry_t, link);
 * ~~~
 * acts as
 * ~~~
 *     static list_type my_list = { empty list };
 * ~~~
 */
#define NS_HSLIST_DEFINE(name, type, field) \
    NS_HSLIST_HEAD(type, field) NS_HSLIST_NAME_INIT(name)

/** \hideinitializer \brief Add an entry to the start of the linked list.
 *
 * \param list  `(list_t *)`           Pointer to list.
 * \param entry `(entry_t * restrict)` Pointer to new entry to add.
 */
#define ns_hslist_add_to_start(list, entry) \
    ns_hslist_add_to_start_(&(list)->slist, NS_HSLIST_OFFSET_(list), NS_HSLIST_TYPECHECK_(list, entry))

/** \hideinitializer \brief Add an entry after a specified entry.
 *
 * \param list  `(list_t *)`           Pointer to list.
 * \param after `(entry_t *)`          Existing entry after which to place the new entry.
 * \param entry `(entry_t * restrict)` Pointer to new entry to add.
 */
#define ns_hslist_add_after(list, after, entry) \
    ns_hslist_add_after_(NS_HSLIST_OFFSET_(list), NS_HSLIST_TYPECHECK_(list, after), NS_HSLIST_TYPECHECK_(list, entry))

/** \brief Check if a list is empty.
 *
 * \param list `(const list_t *)` Pointer to list.
 *
 * \return     `(bool)`           true if the list is empty.
 */
#define ns_hslist_is_empty(list) ((bool) ((list)->slist.first_entry == NULL))

/** \brief Get the first entry.
 *
 * \param list `(const list_t *)` Pointer to list.
 *
 * \return     `(entry_t *)`      Pointer to first entry.
 * \return                        NULL if list is empty.
 */
#define ns_hslist_get_first(list) NS_HSLIST_TYPECAST_(list, (list)->slist.first_entry)

/** \hideinitializer \brief Get the next entry.
 *
 * \param list    `(const list_t *)`  Pointer to list.
 * \param current `(const entry_t *)` Pointer to current entry.
 *
 * \return        `(entry_t *)`       Pointer to next entry.
 * \return                            NULL if current entry is last.
 */
#define ns_hslist_get_next(list, current) \
    NS_HSLIST_TYPECAST_(list, ns_hslist_get_next_(NS_HSLIST_OFFSET_(list), NS_HSLIST_TYPECHECK_(list, current)))

/** \hideinitializer \brief Remove an entry.
 *
 * This is an inefficient O(n) operation, as it requires scanning the list.
 * Use ns_hslist_remove_first or ns_hslist_remove_next in preference,
 * else use ns_list to support efficient random removal.
 *
 * \param list  `(list_t *)`  Pointer to list.
 * \param entry `(entry_t *)` Entry on list to be removed.
 */
#define ns_hslist_remove(list, entry) \
    ns_hslist_remove_(&(list)->slist, NS_HSLIST_OFFSET_(list), NS_HSLIST_TYPECHECK_(list, entry))

/** \hideinitializer \brief Remove the first entry.
 *
 * \param list    `(const list_t *)`  Pointer to list.
 *
 * \return        `(entry_t *)`       Pointer to removed first entry.
 * \return                            NULL if list was empty.
 */
#define ns_hslist_remove_first(list) \
    NS_HSLIST_TYPECAST_(list, ns_hslist_remove_first_(&(list)->slist, NS_HSLIST_OFFSET_(list)))

/** \hideinitializer \brief Remove the next entry.
 *
 * \param list    `(list_t *)`        Pointer to list.
 * \param current `(const entry_t *)` Pointer to current entry.
 *
 * \return        `(entry_t *)`       Pointer to removed next entry.
 * \return                            NULL if current entry was last.
 */
#define ns_hslist_remove_next(list, current) \
    NS_HSLIST_TYPECAST_(list, ns_hslist_remove_next_(NS_HSLIST_OFFSET_(list), NS_HSLIST_TYPECHECK_(list, current)))

/** \hideinitializer \brief Concatenate two lists.
 *
 * Attach the entries on the source list to the end of the destination
 * list, leaving the source list empty.
 *
 * \param dst `(list_t *)` Pointer to destination list.
 * \param src `(list_t *)` Pointer to source list.
 *
 */
#define ns_hslist_concatenate(dst, src) \
        (NS_HSLIST_PTR_MATCH_(dst, src, "concatenating different list types"), \
        ns_hslist_concatenate_(&(dst)->slist, &(src)->slist, NS_HSLIST_OFFSET_(src)))

/** \brief Iterate forwards over a list.
 *
 * Example:
 * ~~~
 *     ns_hslist_foreach(const my_entry_t, cur, &my_list)
 *     {
 *         printf("%s\n", cur->name);
 *     }
 * ~~~
 * Deletion of the current entry is not permitted as its next is checked after
 * running user code.
 *
 * The iteration pointer is declared inside the loop, using C99/C++, so it
 * is not accessible after the loop.  This encourages good code style, and
 * matches the semantics of C++11's "ranged for", which only provides the
 * declaration form:
 * ~~~
 *     for (const my_entry_t cur : my_list)
 * ~~~
 * If you need to see the value of the iteration pointer after a `break`,
 * you will need to assign it to a variable declared outside the loop before
 * breaking:
 * ~~~
 *      my_entry_t *match = NULL;
 *      ns_hslist_foreach(my_entry_t, cur, &my_list)
 *      {
 *          if (cur->id == id)
 *          {
 *              match = cur;
 *              break;
 *          }
 *      }
 * ~~~
 *
 * The user has to specify the entry type for the pointer definition, as type
 * extraction from the list argument isn't portable. On the other hand, this
 * also permits const qualifiers, as in the example above, and serves as
 * documentation. The entry type will be checked against the list type where the
 * compiler supports it.
 *
 * \param type                    Entry type `([const] entry_t)`.
 * \param e                       Name for iteration pointer to be defined
 *                                inside the loop.
 * \param list `(const list_t *)` Pointer to list - evaluated multiple times.
 */
#define ns_hslist_foreach(type, e, list) \
    for (type *e = NS_HSLIST_TYPECOERCE(type *, ns_hslist_get_first(list)); \
        e; e = NS_HSLIST_TYPECOERCE(type *, ns_hslist_get_next(list, e)))

/** \brief Iterate forwards over a list, where user may delete.
 *
 * As ns_hslist_foreach(), but deletion of current entry is permitted as its
 * next pointer is recorded before running user code.
 *
 * Example:
 * ~~~
 *     ns_hslist_foreach_safe(my_entry_t, cur, &my_list)
 *     {
 *         ns_hslist_remove(&my_list, cur);
 *     }
 * ~~~
 * \param type               Entry type `(entry_t)`.
 * \param e                  Name for iteration pointer to be defined
 *                           inside the loop.
 * \param list `(list_t *)`  Pointer to list - evaluated multiple times.
 */
#define ns_hslist_foreach_safe(type, e, list) \
    for (type *e = NS_HSLIST_TYPECOERCE(type *, ns_hslist_get_first(list)), *_next##e; \
        e && (_next##e = NS_HSLIST_TYPECOERCE(type *, ns_hslist_get_next(list, e)), true); e = _next##e)

/** \hideinitializer \brief Count entries on a list
 *
 * Unlike other operations, this is O(n). Note: if list might contain over
 * 65535 entries, this function **must not** be used to get the entry count.
 *
 * \param list `(const list_t *)` Pointer to list.

 * \return     `(uint_fast16_t)`  Number of entries that are stored in list.
 */
#define ns_hslist_count(list) ns_hslist_count_(&(list)->slist, NS_HSLIST_OFFSET_(list))

/** \privatesection
 *  Internal functions - designed to be accessed using corresponding macros above
 */
NS_INLINE void ns_hslist_init_(ns_hslist_t *list);
NS_INLINE void ns_hslist_link_init_(ns_hslist_link_t *link);
NS_INLINE void ns_hslist_add_to_start_(ns_hslist_t *list, ns_hslist_offset_t link_offset, void *restrict entry);
NS_INLINE void ns_hslist_add_after_(ns_hslist_offset_t link_offset, void *after, void *restrict entry);
NS_INLINE void *ns_hslist_get_next_(ns_hslist_offset_t link_offset, const void *current);
NS_INLINE void ns_hslist_remove_(ns_hslist_t *list, ns_hslist_offset_t link_offset, void *entry);
NS_INLINE void *ns_hslist_remove_next_(ns_hslist_offset_t link_offset, void *entry);
NS_INLINE void *ns_hslist_remove_first_(ns_hslist_t *list, ns_hslist_offset_t link_offset);
NS_INLINE uint_fast16_t ns_hslist_count_(const ns_hslist_t *list, ns_hslist_offset_t link_offset);

/* Provide definitions, either for inlining, or for ns_hslist.c */
#if defined NS_ALLOW_INLINING || defined NS_HSLIST_FN
#ifndef NS_HSLIST_FN
#define NS_HSLIST_FN NS_INLINE
#endif

/* Pointer to the link member in entry e */
#define NS_HSLIST_LINK_(e, offset) ((ns_hslist_link_t *)((char *)(e) + offset))

/* Lvalue of the next link pointer in entry e */
#define NS_HSLIST_NEXT_(e, offset) (NS_HSLIST_LINK_(e, offset)->next)

/* Convert a pointer to a link member back to the entry;
 * works for linkptr either being a ns_hslist_link_t pointer, or its next pointer,
 * as the next pointer is first in the ns_hslist_link_t */
#define NS_HSLIST_ENTRY_(linkptr, offset) ((void *)((char *)(linkptr) - offset))

NS_HSLIST_FN void ns_hslist_init_(ns_hslist_t *list)
{
    list->first_entry = NULL;
}

NS_HSLIST_FN void ns_hslist_link_init_(ns_hslist_link_t *link)
{
    NS_FUNNY_INTPTR_OK
    link->next = NS_HSLIST_POISON;
    NS_FUNNY_INTPTR_RESTORE
}

NS_HSLIST_FN void ns_hslist_add_to_start_(ns_hslist_t *list, ns_hslist_offset_t offset, void *restrict entry)
{
    NS_HSLIST_NEXT_(entry, offset) = list->first_entry;

    list->first_entry = entry;
}

NS_HSLIST_FN void ns_hslist_add_after_(ns_hslist_offset_t offset, void *current, void *restrict entry)
{
    NS_HSLIST_NEXT_(entry, offset) = NS_HSLIST_NEXT_(current, offset);

    NS_HSLIST_NEXT_(current, offset) = entry;
}

NS_HSLIST_FN void *ns_hslist_get_next_(ns_hslist_offset_t offset, const void *current)
{
    return NS_HSLIST_NEXT_(current, offset);
}

NS_HSLIST_FN void ns_hslist_remove_(ns_hslist_t *list, ns_hslist_offset_t offset, void *removed)
{
    void **prev_nextptr = &list->first_entry;

    for (void *p = *prev_nextptr; p != removed; prev_nextptr = &NS_HSLIST_NEXT_(p, offset), p = *prev_nextptr) {
    }

    void *next = NS_HSLIST_NEXT_(removed, offset);
    *prev_nextptr = next;

    ns_hslist_link_init_(NS_HSLIST_LINK_(removed, offset));
}

NS_HSLIST_FN void *ns_hslist_remove_next_(ns_hslist_offset_t offset, void *before)
{
    void **prev_nextptr = &NS_HSLIST_NEXT_(before, offset);
    void *removed = *prev_nextptr;
    if (!removed) {
        return NULL;
    }

    *prev_nextptr = NS_HSLIST_NEXT_(removed, offset);

    ns_hslist_link_init_(NS_HSLIST_LINK_(removed, offset));

    return removed;
}

NS_HSLIST_FN void *ns_hslist_remove_first_(ns_hslist_t *list, ns_hslist_offset_t offset)
{
    void *removed = list->first_entry;
    if (!removed) {
        return NULL;
    }

    list->first_entry = NS_HSLIST_NEXT_(removed, offset);

    ns_hslist_link_init_(NS_HSLIST_LINK_(removed, offset));

    return removed;
}

NS_HSLIST_FN uint_fast16_t ns_hslist_count_(const ns_hslist_t *list, ns_hslist_offset_t offset)
{
    uint_fast16_t count = 0;

    for (void *p = list->first_entry; p; p = NS_HSLIST_NEXT_(p, offset)) {
        count++;
    }

    return count;
}
#endif /* defined NS_ALLOW_INLINING || defined NS_HSLIST_FN */

#ifdef __cplusplus
}
#endif

#endif /* NS_HSLIST_H_ */

