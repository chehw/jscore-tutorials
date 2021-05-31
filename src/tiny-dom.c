/*
 * tiny-dom.c
 * 
 * Copyright 2021 chehw <hongwei.che@gmail.com>
 * 
 * The MIT License
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of 
 * this software and associated documentation files (the "Software"), to deal in 
 * the Software without restriction, including without limitation the rights to 
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies 
 * of the Software, and to permit persons to whom the Software is furnished to 
 * do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all 
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
 * SOFTWARE.
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "js-utils.h"
#include "net-utils.h"
#include <libxml/tree.h>
#include <libxml/parser.h>

#include "avl_tree.h"

struct w3c_dom
{
	xmlDoc * doc;
	xmlNode * root;
	
	void * priv;
	void * user_data;
	avl_tree_t elements[1];
	
	xmlNode * (*getElementByTagName)(struct w3c_dom * document, const char * tagName);
};
struct w3c_dom * w3c_dom_init(struct w3c_dom * dom, xmlDoc * doc, xmlNode * root, void * user_data);
void w3c_dom_cleanup(struct w3c_dom * dom);

static int node_name_compare(const void * _a, const void * _b) 
{
	const xmlNode * a = _a;
	const xmlNode * b = _b;
	
	assert(a->type == XML_ELEMENT_NODE);
	assert(b->type == XML_ELEMENT_NODE);
	assert(a->name && b->name);
	return strcasecmp((char *)a->name, (char *)b->name);
}

xmlNode * w3c_dom_get_element_by_tag_name(struct w3c_dom * document, const char * tagName)
{
	xmlNode pattern[1] = {{
		.type = XML_ELEMENT_NODE,
		.name = (void *)tagName,
	}};
	xmlNode ** p_node = avl_tree_find(document->elements, pattern, node_name_compare);
	if(p_node) return *p_node;
	return NULL;
}


void dump_nodes(xmlNode * node, int recursive);
static void dump_properties(xmlAttr * properties)
{
	xmlAttr * cur = NULL;
	for(cur = properties; cur; cur = cur->next) {
		if(cur->type == XML_ATTRIBUTE_NODE) {
			printf("\t%s: ", cur->name);
		}
		dump_nodes(cur->children, 0);
		printf("\n");
	}
	return;
}

void dump_nodes(xmlNode * node, int recursive)
{
	xmlNode * cur = NULL;
	for(cur = node; cur; cur = cur->next) {
		if(cur->type == XML_ELEMENT_NODE) printf("<%s>\n", cur->name);
		else if(cur->type == XML_TEXT_NODE && cur->content) {
			printf("%s", cur->content);
		}
		if(cur->properties) {
			dump_properties(cur->properties);
		}
		
		dump_nodes(cur->children, 1);	// dump all childrens recursively
		if(cur->type == XML_ELEMENT_NODE) printf("</%s>\n", cur->name);
		
		if(!recursive) break;
	}
	return;
}

int main(int argc, char **argv)
{
	const char * url = "http://localhost/libxml2/index.html";
	if(argc > 1) url = argv[1];
	
	struct net_utils_http_client * http = net_utils_http_client_init(NULL, NULL);
	assert(http);
	
	int rc = 0;
	rc = http->set_url(http, url);
	rc = http->send_request(http, "GET", NULL, 0);
	assert(0 == rc);
	
	const char * html = (char *)http->in_buf->data;
	ssize_t cb_html = http->in_buf->length;
	assert(html && cb_html > 0);
	
	//~ printf("=================\n");
	//~ printf("%s\n", html);
	//~ printf("=================\n");
	
	xmlDoc * doc = xmlReadMemory(html, cb_html, url, NULL, XML_PARSE_RECOVER);
	assert(doc);
	
	xmlNode * root = xmlDocGetRootElement(doc);
	assert(root);
	
#ifdef TEST_XMLNODE_ONLY
	dump_nodes(root);
#else
	struct w3c_dom * document = w3c_dom_init(NULL, doc, root, NULL);
	
	printf("find ...\n");
	
	xmlNode * body = document->getElementByTagName(document, "body");
	xmlNode * head = document->getElementByTagName(document, "head");
	xmlNode * title = document->getElementByTagName(document, "title");
	xmlNode * style = document->getElementByTagName(document, "style");
	
	assert(body);
	assert(title);
	assert(head);
	
	dump_nodes(head, 0);
	dump_nodes(title, 0);
	dump_nodes(body, 0);
	dump_nodes(style, 0);

	w3c_dom_cleanup(document);
	free(document);
	
#endif
	
	xmlFreeDoc(doc);
	
	net_utils_http_client_cleanup(http);
	free(http);
	return 0;
}


#include <pthread.h>
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static int dom_tree_travse_for_elements(xmlNode * root, struct w3c_dom * dom)
{
	xmlNode * cur = NULL;
	avl_tree_t * elements = dom->elements;
	for(cur = root; cur; cur = cur->next) {
		if(cur->type == XML_ELEMENT_NODE) {
			pthread_mutex_lock(&s_mutex);
			avl_tree_add(elements, cur, node_name_compare);
			pthread_mutex_unlock(&s_mutex);
		//	printf("add <%s>\n", cur->name);
		}
		dom_tree_travse_for_elements(cur->children, dom);
	}
	return 0;
}

struct w3c_dom * w3c_dom_init(struct w3c_dom * dom, xmlDoc * doc, xmlNode * root, void * user_data)
{
	if(NULL == dom) dom = calloc(1, sizeof(*dom));
	assert(dom);
	
	dom->getElementByTagName =w3c_dom_get_element_by_tag_name;
	avl_tree_init(dom->elements, dom);
	
	dom->user_data = user_data;
	dom->doc = doc;
	dom->root = root;
	
	if(doc && root) { // parse dom and build an avl tree for elements searching
		dom_tree_travse_for_elements(dom->root, dom);
	}
	
	return dom;
}

void w3c_dom_cleanup(struct w3c_dom * dom)
{
	avl_tree_cleanup(dom->elements);
	return;
}
