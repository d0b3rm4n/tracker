/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "ttl_loader.h"
#include <glib/gstdio.h>

#include <libtracker-data/tracker-sparql-query.h>

/* Ontology classes */
#define RDFS_CLASS "http://www.w3.org/2000/01/rdf-schema#Class"
#define RDF_PROPERTY "http://www.w3.org/1999/02/22-rdf-syntax-ns#Property"
#define RDFS_SUBCLASSOF  "http://www.w3.org/2000/01/rdf-schema#subClassOf"
#define RDFS_TYPE "http://www.w3.org/1999/02/22-rdf-syntax-ns#type"
#define RDFS_RANGE "http://www.w3.org/2000/01/rdf-schema#range"
#define RDFS_DOMAIN "http://www.w3.org/2000/01/rdf-schema#domain"
#define RDFS_COMMENT "http://www.w3.org/2000/01/rdf-schema#comment"
#define RDFS_LABEL "http://www.w3.org/2000/01/rdf-schema#label"
#define RDFS_SUBPROPERTYOF "http://www.w3.org/2000/01/rdf-schema#subPropertyOf"

#define NRL_MAX_CARDINALITY "http://www.semanticdesktop.org/ontologies/2007/08/15/nrl#maxCardinality"

/* #define TRACKER_NAMESPACE "http://www.tracker-project.org/ontologies/tracker#Namespace" */
#define TRACKER_NS "http://www.tracker-project.org/ontologies/tracker#"
#define TRACKER_NOTIFY TRACKER_NS "notify"
#define TRACKER_FTS_INDEXED TRACKER_NS "fulltextIndexed"
#define TRACKER_FTS_WEIGHT TRACKER_NS "weight"

#define NAO_DEPRECATED "http://www.semanticdesktop.org/ontologies/2007/08/15/nao#deprecated"

/* Ontology description */
#define DSC_PREFIX "http://www.tracker-project.org/temp/dsc#"

#define DSC_ONTOLOGY DSC_PREFIX "Ontology"
#define DSC_TITLE DSC_PREFIX "title"
#define DSC_DESCRIPTION DSC_PREFIX "description"
#define DSC_AUTHOR DSC_PREFIX "author"
#define DSC_EDITOR DSC_PREFIX "editor"
#define DSC_CONTRIBUTOR DSC_PREFIX "contributor"
#define DSC_GITLOG DSC_PREFIX "gitlog"
#define DSC_UPSTREAM DSC_PREFIX "upstream"
#define DSC_BASEURI DSC_PREFIX "baseUrl"
#define DSC_RELPATH DSC_PREFIX "relativePath"
#define DSC_LOCALPREFIX DSC_PREFIX "localPrefix"
#define DSC_COPYRIGHT DSC_PREFIX "copyright"

static gboolean
string_to_boolean (const gchar *str) {
	if (!g_strcmp0 (str, "true")) {
		return TRUE;
	} else if (!g_strcmp0 (str, "false")) {
		return FALSE;
	} else {
		g_error ("Unable to map '%s' into boolean", str);
	}
}


static void
load_in_memory (Ontology    *ontology,
                const gchar *turtle_subject,
                const gchar *turtle_predicate,
                const gchar *turtle_object)
{
	g_return_if_fail (ontology != NULL);

	if (!g_strcmp0 (turtle_predicate, RDFS_TYPE)) {
		/* It is a definition of class or property */
		if (!g_strcmp0 (turtle_object, RDFS_CLASS)) {
			g_hash_table_insert (ontology->classes,
			                     g_strdup (turtle_subject),
			                     ttl_model_class_new (turtle_subject));

		} else if (!g_strcmp0 (turtle_object, RDF_PROPERTY)) {
			g_hash_table_insert (ontology->properties,
			                     g_strdup (turtle_subject),
			                     ttl_model_property_new (turtle_subject));

		} else {
			/* xxx:a-default-instance a xxx:Class */
			OntologyClass *def;

			def = g_hash_table_lookup (ontology->classes, turtle_object);
			if (def) {
				def->instances = g_list_prepend (def->instances,
				                                 g_strdup (turtle_subject));
			}
			/* g_print ("FIXME Ignoring %s %s %s\n",
			   turtle_subject, turtle_predicate, turtle_object);
			*/
		}

	} else if (!g_strcmp0 (turtle_predicate, RDFS_SUBCLASSOF)) {
		/*
		 * A subclass of B:
		 *  - Add B in A->superclasses list
		 *  - Add A in B->subclasses list (if B is in this ontology!)
		 */
		OntologyClass *def;

		def = g_hash_table_lookup (ontology->classes, turtle_subject);
		if (!def) {
			g_error ("Something wrong");
		}

		def->superclasses = g_list_prepend (def->superclasses,
		                                    g_strdup (turtle_object));

		def = g_hash_table_lookup (ontology->classes, turtle_object);
		if (def) {
			def->subclasses = g_list_prepend (def->subclasses,
			                                  g_strdup (turtle_subject));
		}
	} else if (!g_strcmp0 (turtle_predicate, TRACKER_NOTIFY)) {
		/*
		 * A tracker:notify TRUE
		 */
		OntologyClass *def;

		def = g_hash_table_lookup (ontology->classes, turtle_subject);
		if (!def) {
			g_error ("Something wrong");
		}

		def->notify = string_to_boolean (turtle_object);

	} else if (!g_strcmp0 (turtle_predicate, TRACKER_FTS_INDEXED)) {
		/*
		 * A tracker:fulltextIndexed TRUE
		 */
		OntologyProperty *prop;

		prop = g_hash_table_lookup (ontology->properties, turtle_subject);
		if (!prop) {
			g_error ("Something wrong");
		}

		prop->fulltextIndexed = string_to_boolean (turtle_object);

	} else if (!g_strcmp0 (turtle_predicate, TRACKER_FTS_WEIGHT)) {
		/*
		 * A tracker:weight X
		 */
		OntologyProperty *prop;

		prop = g_hash_table_lookup (ontology->properties, turtle_subject);
		if (!prop) {
			g_error ("Something wrong");
		}

		prop->weight = g_strdup (turtle_object);

	} else if (!g_strcmp0 (turtle_predicate, RDFS_COMMENT)) {
		OntologyClass *klass;
		OntologyProperty *prop;

		klass = g_hash_table_lookup (ontology->classes, turtle_subject);
		if (klass) {
			klass->description = g_strdup (turtle_object);
		} else {
			prop = g_hash_table_lookup (ontology->properties, turtle_subject);
			if (prop) {
				prop->description = g_strdup (turtle_object);
			} else {
				g_error ("UHUMMM %s", turtle_subject);
			}
		}

	} else if (!g_strcmp0 (turtle_predicate, RDFS_DOMAIN)) {
		/*
		 * (prop A) has domain (class B)
		 *  -> add B in A->domain
		 *  -> add A in B->in_domain_of (if B is defined in this ontology!)
		 */
		OntologyProperty *prop;
		OntologyClass *klass;

		prop = g_hash_table_lookup (ontology->properties, turtle_subject);
		if (!prop) {
			g_error ("Strange error in domain (%s doesnt exist!)",
			         turtle_subject);
		}
		prop->domain = g_list_prepend (prop->domain, g_strdup (turtle_object));

		klass = g_hash_table_lookup (ontology->classes, turtle_object);
		if (klass) {
			klass->in_domain_of = g_list_prepend (klass->in_domain_of,
			                                      g_strdup (turtle_subject));
		}

	} else if (!g_strcmp0 (turtle_predicate, RDFS_RANGE)) {
		/*
		 * (prop A) has range (class B)
		 *  -> add B in A->range
		 *  -> add A in B->in_range_of (if B is defined in this ontology!)
		 */
		OntologyProperty *prop;
		OntologyClass *klass;

		prop = g_hash_table_lookup (ontology->properties, turtle_subject);
		if (!prop) {
			g_error ("Strange error in domain (%s doesnt exist!)",
			         turtle_subject);
		}
		prop->range = g_list_prepend (prop->range, g_strdup (turtle_object));

		klass = g_hash_table_lookup (ontology->classes, turtle_object);
		if (klass) {
			klass->in_range_of = g_list_prepend (klass->in_range_of,
			                                     g_strdup (turtle_subject));
		}
	} else if (!g_strcmp0 (turtle_predicate, NRL_MAX_CARDINALITY)) {
		OntologyProperty *prop;

		prop = g_hash_table_lookup (ontology->properties, turtle_subject);
		if (!prop) {
			g_error ("Strange error in max cardinality (%s doesnt exist!)",
			         turtle_subject);
		}
		prop->max_cardinality = g_strdup (turtle_object);

	} else if (!g_strcmp0 (turtle_predicate, RDFS_SUBPROPERTYOF)) {
		/*
		 * (prop A) is subproperty of (prop B)
		 *  -> add B in A->superproperties
		 *  -> add A in B->subproperties (if B is in this ontology)
		 */
		OntologyProperty *propA, *propB;

		propA = g_hash_table_lookup (ontology->properties, turtle_subject);
		if (!propA) {
			g_error ("Strange error in subpropertyof (%s doesnt exist!)",
			         turtle_subject);
		}
		propA->superproperties = g_list_prepend (propA->superproperties,
		                                         g_strdup (turtle_object));

		propB = g_hash_table_lookup (ontology->properties, turtle_object);
		if (propB) {
			propB->subproperties = g_list_prepend (propB->subproperties,
			                                       g_strdup (turtle_subject));
		}

	} else if (!g_strcmp0 (turtle_predicate, NAO_DEPRECATED)) {
		/*
		 * X nao:deprecated true
		 *
		 * This can apply to classes OR properties!
		 */
		OntologyProperty *prop;
		OntologyClass *klass;

		prop = g_hash_table_lookup (ontology->properties, turtle_subject);
		if (prop) {
			prop->deprecated = string_to_boolean (turtle_object);
		} else {
			/* Try with a class */
			klass = g_hash_table_lookup (ontology->classes, turtle_subject);
			if (klass) {
				klass->deprecated = string_to_boolean (turtle_object);
			} else {
				g_error ("'%s' is not a class nor a property!?", turtle_subject);
			}
		}

	} else if (!g_strcmp0 (turtle_predicate, RDFS_LABEL)) {
		/* Intentionaly ignored */
	} else {
		/* DEBUG
		   g_print ("UNHANDLED %s %s %s\n",
		   turtle_subject, turtle_predicate, turtle_object);
		*/
	}

}

static void
load_description (OntologyDescription *desc,
                  const gchar         *turtle_subject,
                  const gchar         *turtle_predicate,
                  const gchar         *turtle_object)
{
	if (!g_strcmp0 (turtle_predicate, RDFS_TYPE)) {
		g_assert (!g_strcmp0 (turtle_object, DSC_ONTOLOGY));
	} else if (!g_strcmp0 (turtle_predicate, DSC_TITLE)) {
		desc->title = g_strdup (turtle_object);
	} else if (!g_strcmp0 (turtle_predicate, DSC_DESCRIPTION)) {
		desc->description = g_strdup (turtle_object);
	} else if (!g_strcmp0 (turtle_predicate, DSC_UPSTREAM)) {
		desc->upstream = g_strdup (turtle_object);
	} else if (!g_strcmp0 (turtle_predicate, DSC_AUTHOR)) {
		desc->authors = g_list_prepend (desc->authors, g_strdup (turtle_object));
	} else if (!g_strcmp0 (turtle_predicate, DSC_EDITOR)) {
		desc->editors = g_list_prepend (desc->editors, g_strdup (turtle_object));
	} else if (!g_strcmp0 (turtle_predicate, DSC_CONTRIBUTOR)) {
		desc->contributors = g_list_prepend (desc->contributors,
		                                     g_strdup (turtle_object));
	} else if (!g_strcmp0 (turtle_predicate, DSC_GITLOG)) {
		desc->gitlog = g_strdup (turtle_object);
	} else if (!g_strcmp0 (turtle_predicate, DSC_BASEURI)) {
		desc->baseUrl = g_strdup (turtle_object);
	} else if (!g_strcmp0 (turtle_predicate, DSC_RELPATH)) {
		desc->relativePath = g_strdup (turtle_object);
	} else if (!g_strcmp0 (turtle_predicate, DSC_LOCALPREFIX)) {
		desc->localPrefix = g_strdup (turtle_object);
	} else if (!g_strcmp0 (turtle_predicate, DSC_COPYRIGHT)) {
		desc->copyright = g_strdup (turtle_object);
	} else {
		g_critical ("Unhandled element %s", turtle_predicate);
	}
}


Ontology *
ttl_loader_load_ontology (const gchar *ttl_file)
{
	Ontology *ontology;

	ontology = g_new0 (Ontology, 1);
	ontology->classes = g_hash_table_new_full (g_str_hash,
	                                           g_str_equal,
	                                           g_free,
	                                           (GDestroyNotify)ttl_model_class_free);

	ontology->properties = g_hash_table_new_full (g_str_hash,
	                                              g_str_equal,
	                                              g_free,
	                                              (GDestroyNotify)ttl_model_property_free);

	if (ttl_file) {
		TrackerTurtleReader *reader;
		GError *error = NULL;

		reader = tracker_turtle_reader_new (ttl_file, NULL);

		while (error == NULL && tracker_turtle_reader_next (reader, &error)) {
			load_in_memory (ontology,
			                tracker_turtle_reader_get_subject (reader),
			                tracker_turtle_reader_get_predicate (reader),
			                tracker_turtle_reader_get_object (reader));
		}

		g_object_unref (reader);

		if (error) {
			g_message ("Turtle parse error: %s", error->message);
			g_error_free (error);
		}
	} else {
		g_warning ("Unable to open '%s'", ttl_file);
	}

	return ontology;
}

OntologyDescription *
ttl_loader_load_description (const gchar *filename)
{
	OntologyDescription *desc;
	TrackerTurtleReader *reader;
	GError *error = NULL;

	desc = ttl_model_description_new ();


	reader = tracker_turtle_reader_new (filename, NULL);

	while (error == NULL && tracker_turtle_reader_next (reader, &error)) {
		load_description (desc,
		                  tracker_turtle_reader_get_subject (reader),
		                  tracker_turtle_reader_get_predicate (reader),
		                  tracker_turtle_reader_get_object (reader));
	}

	g_object_unref (reader);

	if (error) {
		g_message ("Turtle parse error: %s", error->message);
		g_error_free (error);
	}

	return desc;
}


void
ttl_loader_free_ontology (Ontology *ontology)
{
	g_hash_table_destroy (ontology->classes);
	g_hash_table_destroy (ontology->properties);
	g_free (ontology);
}

void
ttl_loader_free_description (OntologyDescription *desc)
{
	ttl_model_description_free (desc);
}
