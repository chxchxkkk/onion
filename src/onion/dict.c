/*
	Onion HTTP server library
	Copyright (C) 2010 David Moreno Montero

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as
	published by the Free Software Foundation, either version 3 of the
	License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	*/

#include <malloc.h>
#include <string.h>

#include "log.h"
#include "dict.h"
#include "types_internal.h"

typedef struct onion_dict_node_data_t{
	const char *key;
	const char *value;
	char flags;
}onion_dict_node_data;

/**
 * @short Node for the tree.
 * 
 * Its implemented as a AA Tree (http://en.wikipedia.org/wiki/AA_tree)
 */
typedef struct onion_dict_node_t{
	onion_dict_node_data data;
	int level; 
	struct onion_dict_node_t *left;
	struct onion_dict_node_t *right;
}onion_dict_node;

static void onion_dict_node_data_free(onion_dict_node_data *dict);
static void onion_dict_set_node_data(onion_dict_node_data *data, const char *key, const char *value, int flags);
static onion_dict_node *onion_dict_node_new(const char *key, const char *value, int flags);

/**
 * Initializes the basic tree with all the structure in place, but empty.
 */
onion_dict *onion_dict_new(){
	onion_dict *dict=malloc(sizeof(onion_dict));
	memset(dict,0,sizeof(onion_dict));
	return dict;
}

/**
 * @short Searchs for a given key, and returns that node and its parent (if parent!=NULL) 
 *
 * If not found, returns the parent where it should be. Nice for adding too.
 */
static const onion_dict_node *onion_dict_find_node(const onion_dict_node *current, const char *key, const onion_dict_node **parent){
	if (!current){
		return NULL;
	}
	signed char cmp=strcmp(key, current->data.key);
	//ONION_DEBUG0("%s cmp %s = %d",key, current->data.key, cmp);
	if (cmp==0)
		return current;
	if (parent) *parent=current;
	if (cmp<0)
		return onion_dict_find_node(current->left, key, parent);
	if (cmp>0)
		return onion_dict_find_node(current->right, key, parent);
	return NULL;
}


/// Allocates a new node data, and sets the data itself.
static onion_dict_node *onion_dict_node_new(const char *key, const char *value, int flags){
	onion_dict_node *node=malloc(sizeof(onion_dict_node));

	onion_dict_set_node_data(&node->data, key, value, flags);
	
	node->left=NULL;
	node->right=NULL;
	node->level=1;
	return node;
}

/// Sets the data on the node, on the right way.
static void onion_dict_set_node_data(onion_dict_node_data *data, const char *key, const char *value, int flags){
	//ONION_DEBUG("Set data %02X %02X %02X",flags,OD_DUP_KEY, OD_DUP_VALUE);
	if ((flags&OD_DUP_KEY)==OD_DUP_KEY) // not enought with flag, as its a multiple bit flag, with FREE included
		data->key=strdup(key);
	else
		data->key=key;
	if ((flags&OD_DUP_VALUE)==OD_DUP_VALUE)
		data->value=strdup(value);
	else
		data->value=value;
	data->flags=flags;
}

/// Perform the skew operation
static onion_dict_node *skew(onion_dict_node *node){
	if (!node || !node->left || (node->left->level != node->level))
		return node;

	//ONION_DEBUG("Skew %p[%s]",node,node->data.key);
	onion_dict_node *t;
	t=node->left;
	node->left=t->right;
	t->right=node;
	return t;
}

/// Performs the split operation
static onion_dict_node *split(onion_dict_node *node){
	if (!node || !node->right || !node->right->right || (node->level != node->right->right->level))
		return node;
	
	//ONION_DEBUG("Split %p[%s]",node,node->data.key);
	onion_dict_node *t;
	t=node->right;
	node->right=t->left;
	t->left=node;
	t->level++;
	return t;
}

/// Decrease a level
static void decrease_level(onion_dict_node *node){
	int level_left=node->left ? node->left->level : 0;
	int level_right=node->right ? node->right->level : 0;
	int should_be=((level_left<level_right) ? level_left : level_right) + 1;
	if (should_be < node->level){
		//ONION_DEBUG("Decrease level %p[%s] level %d->%d",node, node->data.key, node->level, should_be);
		node->level=should_be;
		//ONION_DEBUG0("%p",node->right);
		if (node->right && ( should_be < node->right->level) ){
			//ONION_DEBUG("Decrease level right %p[%s], level %d->%d",node->right, node->right->data.key, node->right->level, should_be);
			node->right->level=should_be;
		}
	}
}

/**
 * @short AA tree insert
 * 
 * Returns the root node of the subtree
 */
static onion_dict_node  *onion_dict_node_add(onion_dict_node *node, onion_dict_node *nnode){
	if (node==NULL){
		//ONION_DEBUG("Add here %p",nnode);
		return nnode;
	}
	signed int cmp=strcmp(nnode->data.key, node->data.key);
	//ONION_DEBUG0("cmp %d",cmp);
	if (cmp<0){
		node->left=onion_dict_node_add(node->left, nnode);
		//ONION_DEBUG("%p[%s]->left=%p[%s]",node, node->data.key, node->left, node->left->data.key);
	}
	else{ // >=
		node->right=onion_dict_node_add(node->right, nnode);
		//ONION_DEBUG("%p[%s]->right=%p[%s]",node, node->data.key, node->right, node->right->data.key);
	}
	
	node=skew(node);
	node=split(node);
	
	return node;
}


/**
 * Adds a value in the tree.
 */
void onion_dict_add(onion_dict *dict, const char *key, const char *value, int flags){
	dict->root=onion_dict_node_add(dict->root, onion_dict_node_new(key, value, flags));
}

/// Frees the memory, if necesary of key and value
static void onion_dict_node_data_free(onion_dict_node_data *data){
	if (data->flags&OD_FREE_KEY){
		free((char*)data->key);
	}
	if (data->flags&OD_FREE_VALUE)
		free((char*)data->value);
}

/// AA tree remove the node
static onion_dict_node *onion_dict_node_remove(onion_dict_node *node, const char *key){
	if (!node)
		return NULL;
	int cmp=strcmp(key, node->data.key);
	if (cmp<0){
		node->left=onion_dict_node_remove(node->left, key);
		//ONION_DEBUG("%p[%s]->left=%p[%s]",node, node->data.key, node->left, node->left ? node->left->data.key : "NULL");
	}
	else if (cmp>0){
		node->right=onion_dict_node_remove(node->right, key);
		//ONION_DEBUG("%p[%s]->right=%p[%s]",node, node->data.key, node->right, node->right ? node->right->data.key : "NULL");
	}
	else{ // Real remove
		//ONION_DEBUG("Remove here %p", node);
		onion_dict_node_data_free(&node->data);
		if (node->left==NULL && node->right==NULL){
			free(node);
			return NULL;
		}
		if (node->left==NULL){
			onion_dict_node *t=node->right; // Get next key node
			while (t->left) t=t->left;
			//ONION_DEBUG("Set data from %p[%s] to %p[already deleted %s]",t,t->data.key, node, key);
			memcpy(&node->data, &t->data, sizeof(onion_dict_node_data));
			t->data.flags=0; // No double free later, please
			node->right=onion_dict_node_remove(node->right, t->data.key);
			//ONION_DEBUG("%p[%s]->right=%p[%s]",node, node->data.key, node->right, node->right ? node->right->data.key : "NULL");
		}
		else{
			onion_dict_node *t=node->left; // Get prev key node
			while (t->right) t=t->right;
			
			memcpy(&node->data, &t->data, sizeof(onion_dict_node_data));
			t->data.flags=0; // No double free later, please
			node->left=onion_dict_node_remove(node->left, t->data.key);
			//ONION_DEBUG("%p[%s]->left=%p[%s]",node, node->data.key, node->left, node->left ? node->left->data.key : "NULL");
		}
	}
	decrease_level(node);
	node=skew(node);
	if (node->right){
		node->right=skew(node->right);
		if (node->right->right)
			node->right->right=skew(node->right->right);
	}
	node=split(node);
	if (node->right)
		node->right=split(node->right);
	return node;
}


/**
 * Removes the given key. 
 *
 * Returns if it removed any node.
 */ 
int onion_dict_remove(onion_dict *dict, const char *key){
	dict->root=onion_dict_node_remove(dict->root, key);
	return 1;
}


static void onion_dict_node_free(onion_dict_node *node){
	if (node->left)
		onion_dict_node_free(node->left);
	if (node->right)
		onion_dict_node_free(node->right);

	onion_dict_node_data_free(&node->data);
	free(node);
}

/// Removes the full dict struct form mem.
void onion_dict_free(onion_dict *dict){
	if (dict->root)
		onion_dict_node_free(dict->root);
	free(dict);
}
	

/// Gets a value
const char *onion_dict_get(const onion_dict *dict, const char *key){
	const onion_dict_node *r;
	r=onion_dict_find_node(dict->root, key, NULL);
	if (r)
		return r->data.value;
	return NULL;
}

static void onion_dict_node_print_dot(const onion_dict_node *node){
	if (node->right){
		fprintf(stderr,"\"%s\" -> \"%s\" [label=\"R\"];\n",node->data.key, node->right->data.key);
		onion_dict_node_print_dot(node->right);
	}
	if (node->left){
		fprintf(stderr,"\"%s\" -> \"%s\" [label=\"L\"];\n",node->data.key, node->left->data.key);
		onion_dict_node_print_dot(node->left);
	}
}

/**
 * Prints a graph on the form:
 *
 * key1 -> key0;
 * key1 -> key2;
 * ...
 *
 * User of this function has to write the 'digraph G{' and '}'
 */
void onion_dict_print_dot(const onion_dict *dict){
	if (dict->root)
		onion_dict_node_print_dot(dict->root);
}

void onion_dict_node_preorder(const onion_dict_node *node, void *func, void *data){
	void (*f)(const char *key, const char *value, void *data);
	f=func;
	if (node->left)
		onion_dict_node_preorder(node->left, func, data);
	
	f(node->data.key, node->data.value, data);
	
	if (node->right)
		onion_dict_node_preorder(node->right, func, data);
}

/**
 * @short Executes a function on each element, in preorder by key.
 * 
 * The function is of prototype void func(const char *key, const char *value, void *data);
 */
void onion_dict_preorder(const onion_dict *dict, void *func, void *data){
	if (!dict || !dict->root)
		return;
	onion_dict_node_preorder(dict->root, func, data);
}

static int onion_dict_node_count(const onion_dict_node *node){
	int c=1;
	if (node->left)
		c+=onion_dict_node_count(node->left);
	if (node->right)
		c+=onion_dict_node_count(node->right);
	return c;
}

/**
 * @short Counts elements
 */
int onion_dict_count(const onion_dict *dict){
	if (dict && dict->root)
		return onion_dict_node_count(dict->root);
	return 0;
}