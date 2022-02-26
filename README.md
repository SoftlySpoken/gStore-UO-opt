This repository is the implementation of algorithms described in the paper "Efficient Execution of SPARQL Queries with OPTIONAL and UNION Expressions--A Graph Approach."

## Installation and Usage

To compile, first run `make pre`, then run `make`. If compilation is unsuccessful due to a lack of system dependencies, please run the setup script under the directory `scripts/setup` corresponding to your environment.

To run a SPARQL query on a dataset, please first build the dataset by running:

```bash
$ bin/gbuild <database_name> <dataset_path>
```

Then run the query:

```bash
$ bin/gquery <database_name> <query_path>
```

The optimizations in our paper are implemented in the function `GeneralEvaluation::highLevelOpt` (Query/GeneralEvaluation.cpp).

## Datasets

Since the datasets we use in our experimental study are quite large (exceeding 100G), and can be easily obtained online, we provide their URLs below.

- Real dataset: [DBpedia](https://wiki.dbpedia.org). The data dump we use in our experiments can be downloaded [here](http://downloads.dbpedia.org/3.9/en/) (all the .nt files need to be concatenated into a single file for gStore to process it).

- Generators of synthetic datasets: [LUBM](http://swat.cse.lehigh.edu/projects/lubm/).

## Queries Used in Experiments

### Queries on LUBM

#### q1.1

```sparql
SELECT * WHERE { <http://www.Department0.University0.edu/UndergraduateStudent91> ub:memberOf ?v1. { ?v2 ub:headOf ?v1. } UNION { ?v2 ub:worksFor ?v1. } ?v2 ub:undergraduateDegreeFrom ?v3.
?v4 ub:doctoralDegreeFrom ?v3. ?v5 ub:publicationAuthor ?v2.
{ ?v6 ub:headOf ?v1. } UNION { ?v6 ub:worksFor ?v1. }
{ ?v2 ub:headOf ?v7. } UNION { ?v2 ub:worksFor ?v7. } ?v7 ub:name ?v8. }
```

#### q1.2

```sparql
SELECT * WHERE { <http://www.Department1.University0.edu/UndergraduateStudent363> ub:takesCourse ?v1. OPTIONAL{ ?v2 ub:teachingAssistantOf ?v1. OPTIONAL{ ?v2 ub:memberOf ?v3. ?v4 ub:subOrganizationOf ?v3.
?v4 ub:subOrganizationOf ?v5. ?v4 rdf:type ?v6.
OPTIONAL{ ?v5 ub:subOrganizationOf ?v7. } } } }
```

#### q1.3

```sparql
SELECT * WHERE { <http://www.Department0.University0.edu/UndergraduateStudent356> ub:memberOf ?v1. { ?v2 ub:worksFor ?v1. } UNION { ?v2 ub:headOf ?v1. } ?v1 rdf:type ?v3. OPTIONAL{ ?v4 ub:advisor ?v2.
OPTIONAL{ ?v4 ub:teachingAssistantOf ?v5. ?v4 ub:name ?v6. } } OPTIONAL{ ?v7 ub:advisor ?v2. } }
```

#### q1.4

```sparql
SELECT * WHERE {
?v1 ub:emailAddress "UndergraduateStudent309@Department12.University0.edu". OPTIONAL{ ?v1 ub:memberOf ?v2. ?v2 ub:name ?v3.
OPTIONAL{ { ?v4 ub:worksFor ?v2. } UNION { ?v4 ub:headOf ?v2. } ?v5 ub:publicationAuthor ?v4.
OPTIONAL{ ?v6 ub:publicationAuthor ?v4. } } } }
```

#### q1.5

```sparql
SELECT * WHERE { <http://www.Department1.University0.edu/UndergraduateStudent256> ub:memberOf ?v1.
{ ?v2 ub:worksFor ?v1. } UNION { ?v2 ub:headOf ?v1. }
{ ?v2 ub:worksFor ?v3. } UNION { ?v2 ub:headOf ?v3. }
?v4 ub:headOf ?v1. ?v3 ub:subOrganizationOf ?v5.
OPTIONAL{ ?v6 ub:publicationAuthor ?v2. }
OPTIONAL{ { ?v7 ub:headOf ?v1. } UNION { ?v7 ub:worksFor ?v1. } } }
```

#### q2.1

```sparql
SELECT * WHERE {
{ ?st ub:teachingAssistantOf ?course.
OPTIONAL { ?st ub:takesCourse ?course2. ?pub1 ub:publicationAuthor ?st. } } { ?prof ub:teacherOf ?course. ?st ub:advisor ?prof.
OPTIONAL { ?prof ub:researchInterest ?resint.
?pub2 ub:publicationAuthor ?prof. } } }
```

#### q2.2

```sparql
SELECT * WHERE {
{ ?pub rdf:type ub:Publication. ?pub ub:publicationAuthor ?st.
?pub ub:publicationAuthor ?prof.
OPTIONAL { ?st ub:emailAddress ?ste. ?st ub:telephone ?sttel. } }
{ ?st ub:undergraduateDegreeFrom ?univ. ?dept ub:subOrganizationOf ?univ. OPTIONAL { ?head ub:headOf ?dept. ?others ub:worksFor ?dept. } }
{ ?st ub:memberOf ?dept. ?prof ub:worksFor ?dept.
OPTIONAL { ?prof ub:doctoralDegreeFrom ?univ1.
?prof ub:researchInterest ?resint1. } } }
```

#### q2.3

```sparql
SELECT * WHERE {
{ ?pub ub:publicationAuthor ?st. ?pub ub:publicationAuthor ?prof.
?st rdf:type ub:GraduateStudent.
OPTIONAL { ?st ub:undergraduateDegreeFrom ?univ1.
?st ub:telephone ?sttel. } } { ?st ub:advisor ?prof.
OPTIONAL { ?prof ub:doctoralDegreeFrom ?univ.
?prof ub:researchInterest ?resint. } }
{ ?st ub:memberOf ?dept. ?prof ub:worksFor ?dept. ?prof a ub:FullProfessor. OPTIONAL { ?head ub:headOf ?dept. ?others ub:worksFor ?dept. } } }
```

#### q2.4

```sparql
SELECT * WHERE {
?x ub:worksFor <http://www.Department0.University0.edu>.
?x a ub:FullProfessor.
OPTIONAL { ?y ub:advisor ?x. ?x ub:teacherOf ?z. ?y ub:takesCourse ?z. } }
```

#### q2.5

```sparql
SELECT * WHERE {
?x ub:worksFor <http://www.Department0.University12.edu>.
?x a ub:FullProfessor.
OPTIONAL { ?y ub:advisor ?x. ?x ub:teacherOf ?z. ?y ub:takesCourse ?z. } }
```

#### q2.6

```sparql
SELECT * WHERE {
?x ub:worksFor <http://www.Department0.University12.edu>.
?x a ub:FullProfessor.
OPTIONAL { ?x ub:emailAddress ?y1. ?x ub:telephone ?y2. ?x ub:name ?y3. } }
```

### Queries on DBpedia

#### q1.1

```sparql
SELECT * WHERE {
?v1 dbo:wikiPageWikiLink dbr:Economic_system.
?v1 nsprov:wasDerivedFrom ?v2.
{ ?v1 purl:subject ?v3. } UNION { ?v3 skos:subject ?v1. }
?v3 rdfs:label ?v4. ?v5 nsprov:wasDerivedFrom ?v2. ?v1 owl:sameAs ?v6. { ?v3 rdfs:label ?v7. } UNION { ?v3 foaf:name ?v7. }
{ ?v5 purl:subject ?v8. } UNION { ?v8 skos:subject ?v5. } }
```

#### q1.2

```sparql
SELECT * WHERE {
dbr:AdolfHitler foaf:isPrimaryTopicOf ?v1. ?v2 foaf:isPrimaryTopicOf ?v1. OPTIONAL{ ?v2 dbo:wikiPageRedirects ?v3. ?v4 foaf:primaryTopic ?v2. OPTIONAL{ ?v5 dbo:wikiPageWikiLink ?v3.
OPTIONAL{ ?v6 dbo:wikiPageRedirects ?v5.
OPTIONAL{ ?v6 dbo:wikiPageWikiLink ?v7. } } } } }
```

#### q1.3

```sparql
SELECT * WHERE {
?v1 dbo:wikiPageWikiLink dbr:Abdul_Rahim_Wardak.
?v1 owl:sameAs ?v2. ?v3 owl:sameAs ?v2.
{ ?v3 purl:subject ?v4. } UNION { ?v3 dbo:wikiPageWikiLink ?v4. } ?v3 dbo:wikiPageWikiLink ?v1.
{ ?v4 skos:prefLabel ?v5. } UNION { ?v4 rdfs:label ?v5. } OPTIONAL{ ?v6 owl:sameAs ?v2.
OPTIONAL{ ?v6 dbo:wikiPageLength ?v7. } } }
```

#### q1.4

```sparql
SELECT * WHERE {
dbr:Functional_neuroimaging purl:subject ?v1.
OPTIONAL{ ?v1 owl:sameAs ?v2. ?v1 rdf:type ?v3.
?v4 owl:sameAs ?v2. ?v5 skos:related ?v4. OPTIONAL{ ?v6 skos:related ?v4. } OPTIONAL{ { ?v7 purl:subject ?v1. } UNION { ?v1 skos:subject ?v7. } OPTIONAL{ { ?v7 purl:subject ?v8. } UNION { ?v8 skos:subject ?v7. } } } } }
```

#### q1.5

```sparql
SELECT * WHERE {
?v1 dbo:wikiPageWikiLink dbr:Category:Cell_biology.
{ ?v2 foaf:primaryTopic ?v1. } UNION { ?v1 foaf:isPrimaryTopicOf ?v2. }
{ ?v2 foaf:primaryTopic ?v3. } UNION { ?v3 foaf:isPrimaryTopicOf ?v2. } ?v3 dbo:wikiPageWikiLink ?v1. OPTIONAL{ { ?v2 foaf:primaryTopic ?v4. } UNION { ?v4 foaf:isPrimaryTopicOf ?v2. } }
OPTIONAL{ ?v5 dbo:phylum ?v3. ?v6 dbo:phylum ?v3.
OPTIONAL{ { ?v7 foaf:primaryTopic ?v5. }
UNION { ?v5 foaf:isPrimaryTopicOf ?v7. } } } }
```

#### q2.1

```sparql
SELECT * WHERE {
{ ?v6 a dbo:PopulatedPlace. ?v6 dbo:abstract ?v1. ?v6 rdfs:label ?v2. ?v6 geo:lat ?v3. ?v6 geo:long ?v4. OPTIONAL { ?v6 foaf:depiction ?v8. } }
OPTIONAL { ?v6 foaf:homepage ?v10. } OPTIONAL { ?v6 dbo:populationTotal ?v12. } OPTIONAL { ?v6 dbo:thumbnail ?v14. } }
```

#### q2.2

```sparql
SELECT * WHERE {
?v3 foaf:homepage ?v0. ?v3 a dbo:SoccerPlayer. ?v3 dbp:position ?v6. ?v3 dbp:clubs ?v8. ?v8 dbo:capacity ?v1. ?v3 dbo:birthPlace ?v5. OPTIONAL { ?v3 dbo:number ?v9. } }
```

#### q2.3

```sparql
SELECT * WHERE {
?v5 dbo:thumbnail ?v4. ?v5 rdf:type dbo:Person. ?v5 rdfs:label ?v. ?v5 foaf:homepage ?v8. OPTIONAL { ?v5 foaf:homepage ?v10. } }
```

#### q2.4

```sparql
SELECT * WHERE {
{ ?v2 a dbo:Settlement. ?v2 rdfs:label ?v. ?v6 a dbo:Airport. ?v6 dbo:city ?v2. ?v6 dbp:iata ?v5.
OPTIONAL { ?v6 foaf:homepage ?v7. } }
OPTIONAL { ?v6 dbp:nativename ?v8. } }
```

#### q2.5

```sparql
SELECT * WHERE {
?v4 skos:subject ?v. ?v4 foaf:name ?v6. OPTIONAL { ?v4 rdfs:comment ?v8. } }
```

#### q2.6

```sparql
SELECT * WHERE {
?v0 rdfs:comment ?v1. ?v0 foaf:page ?v. OPTIONAL { ?v0 skos:subject ?v6. } OPTIONAL { ?v0 dbp:industry ?v5. } OPTIONAL { ?v0 dbp:location ?v2. } OPTIONAL { ?v0 dbp:locationCountry ?v3. }
OPTIONAL { ?v0 dbp:locationCity ?v9. ?a dbp:manufacturer ?v0. } OPTIONAL { ?v0 dbp:products ?v11. ?b dbp:model ?v0. }
OPTIONAL { ?v0 georss:point ?v10. }
OPTIONAL { ?v0 rdf:type ?v7. } }
```
