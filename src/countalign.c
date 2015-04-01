/*

The MIT License (MIT)

Copyright (c) 2015 Jennifer Rondineau

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sam.h"
#include "sam_header.h"
#include "debug.h"
#include <errno.h>
#include "bwa.c"
#include "kseq.h" 
#include "bwamem.h"

#define VERIFY_NOT_NULL(POINTER) do{if(POINTER==NULL) { fprintf(stderr,"Memory Alloc failed File %s Line%d.\n",__FILE__,__LINE__); exit(EXIT_FAILURE);}}while(0)
				
#define ALIGN_CONTAMINANTS \
			int len, n,j; \
			opt = mem_opt_init(); \
			for(n=0; n<fastq_count;n++) {\
					len= strlen(fastq[n].seq);\
					mem_alnreg_v ar;\
					int i ;\
					ar = mem_align1(opt, idx->bwt, idx->bns, idx->pac, len, fastq[n].seq);\
				for (i = 0; i < ar.n; ++i) {\
					mem_aln_t a;\
					if (ar.a[i].secondary >= 0) continue; \
					a = mem_reg2aln(opt, idx->bns, idx->pac, len, fastq[n].seq, &ar.a[i]); \
					printf("%s%s\t%c\t%s\t%ld\t%d\t\n", fastq[n].qname, fastq[n].read, "+-"[a.is_rev], idx->bns->anns[a.rid].name, (long)a.pos, a.mapq);\
					Contaminants* searchContaminant = NULL;\
					for (j=0; j<count_contaminants; j++) {\
						if (strcmp (contaminant[j].c_name, idx->bns->anns[a.rid].name) == 0)\
							{\
							searchContaminant = &contaminant[j];\
							contaminant[j].contaminants_count++;\
							break;\
							}\
						}\
					if (searchContaminant == NULL ) {\
						contaminant = (Contaminants*)realloc(contaminant,sizeof(Contaminants)*(count_contaminants+1)); \
						VERIFY_NOT_NULL(contaminant);\
						contaminant[count_contaminants].c_name = strdup(idx->bns->anns[a.rid].name);\
						VERIFY_NOT_NULL(contaminant[count_contaminants].c_name);\
						contaminant[count_contaminants].contaminants_count=1;\
						searchContaminant=&contaminant[count_contaminants];\
						count_contaminants++;\
					}\
				}\
			free(fastq[n].qname);\
			free(fastq[n].seq);\
			free(fastq[n].qual);\
			free(ar.a);\
			}\
			free(opt)
				
				
#define DUMP_FASTQ if ( file_fastq != NULL) {\
		   for (i=0; i < fastq_count; i++)\
			{\
			fprintf(file_fastq, "@%s%s\n%s\n+\n%s\n", fastq[i].qname, fastq[i].read, fastq[i].seq, fastq[i].qual);\
			}\
		   } else \
			{\
			fprintf(stderr,"Unsaved fasta file.\n");\
		   }
		   
typedef struct Contaminants
	{
	char* c_name;
	float contaminants_count;
	}Contaminants;

typedef struct Fastq
	{
	char* qname;
	char* read;
	char* seq;
	char* qual;
	}Fastq;


typedef struct SampleName
 	{
 	char* sample_name; /* must be unique */
 	long unMap;
 	Fastq* fastq;
 	}Sample;
 	
typedef struct Group
 	{
 	long unMap;
 	char* rgId;/* must be unique */
 	Sample* sample;
 	}Group;




static void usage ()
{ 
/* cf isatty */
fprintf(stderr,
"\nUsage : cat file.bam | ./countalign -r file.fa -s file.fastq -o file.txt > file.bam \n\
\n\
Description : \n\
\n\
This program reads BAM file and looks for unmapped reads. It released a report containing the number of unmapped reads by sample. \n\
\n\
Options :\n\
\n\
         -o \033[4mFILE\033[0m   Name of the output file containing the report of unmapped reads(*.txt).\n\
\n\
         -O \033[4mFILE\033[0m   Name of the output BAM file (*.bam).\n\
\n\
	 -s \033[4mFILE\033[0m   Save FASTQ file (*.fastq).\n\
\n\
	 -r \033[4mFILE\033[0m   Reference file (*.fa).\n\
\n\
         -h        Output help and exit immediately. \n\
\n\
         -v        Output version and exit immediately. \n\n");
}



static void version ()
{
printf(" Version : v.1 \n");
}



static int compareGroup(const void *g1, const void *g2)
{
	Group *group1 = (Group *) g1;
	Group *group2 = (Group *) g2;
	return strcmp (group1->rgId, group2->rgId);
}


static int compareContaminant(const void *c1, const void *c2)
{
	Contaminants *contaminant1 = (Contaminants *) c1;
	Contaminants *contaminant2 = (Contaminants *) c2;
	return contaminant2->contaminants_count - contaminant1->contaminants_count;
}



int main(int argc, char** argv)
	{
	FILE* file=NULL;
	FILE* file_fastq=NULL;
	int c, i=0, sample_count=0, group_count=0, fastq_count=0, count_contaminants=0;
	bam_hdr_t *header=NULL;
	bam1_t *b = NULL;
	b = bam_init1();
	float nReads=0;
	const char *rgId=NULL, *sampleName=NULL;	
	Sample* samples=NULL;
	Contaminants* contaminant=NULL;
	Group* group=NULL;
	Fastq* fastq=NULL;
	uint8_t *search = NULL, *seq= NULL, *qual = NULL;
	char *qname = NULL, *read = NULL, *output_report, *filename_out =NULL, *ref=NULL;
	const char *filename_fastq = NULL; 
	int8_t *buf = NULL;
	int32_t qlen =NULL;
	bwaidx_t *idx;
	mem_opt_t *opt;
		
	
	// get commande line options
	 while ((c = getopt(argc, argv,"r:o:O:hvs:")) != -1) {
		 switch(c)
			 {
			 case 'h': 
			 	{
			 	usage();
			 	return EXIT_SUCCESS;
			 	break;
			 	}
			 case 'v':
			 	{
			 	version();
			 	return EXIT_SUCCESS;
			 	break;
			 	}
			 
			 case 'o': 
			 	{
			 	output_report = strdup(optarg); 
			 	break;
			 	}
			 case 's': 
			 	{
			 	filename_fastq = strdup(optarg);
			 	if( filename_fastq == NULL )
			 		{
			 		fprintf(stderr,"Cannot alloc memory.\n");
			 		return EXIT_FAILURE;
			 		}
			 	break;
			 	}
			  case 'r': 
			 	{
			 	ref = strdup(optarg);
			 	if( ref == NULL )
			 		{
			 		fprintf(stderr,"Cannot alloc memory.\n");
			 		return EXIT_FAILURE;
			 		}
			 	break;
			 	}
			 case 'O' :
			 	{
			 	filename_out = strdup(optarg);
			 	if( filename_out == NULL )
			 		{
			 		fprintf(stderr,"Cannot alloc memory.\n");
			 		return EXIT_FAILURE;
			 		}
			 	break;
			 	}
			case '?': 
				{
				 fprintf(stderr, "ERROR: option -%c is undefined\n", optopt);
			 	 return EXIT_FAILURE;
			 	 break;
			 	 }
			case ':': 
				{
				fprintf(stderr, "ERROR: option -%c requires an argument\n",optopt);
				return EXIT_FAILURE;
			 	break;
			 	}
			default :
				{
				usage(); 
				return EXIT_FAILURE;
				break;
				}
			 }
	 }
	

	if (filename_fastq != NULL) 
			{
			file_fastq=fopen(filename_fastq,"w+");
			if ( file_fastq == NULL) 
 				{
		      	 	fprintf(stderr,"Cannot read file. %s.\n",strerror(errno));
		      	 	return EXIT_FAILURE;
	      	 		}     	 	
			}		
			
	
	samFile *fp = sam_open( "-" ); 
	
		if (fp == NULL) 
			{
        		fprintf(stderr,"Cannot read file. %s.\n",strerror(errno));
        		return EXIT_FAILURE;
			}

	header = sam_hdr_read(fp);

		if( header == NULL)
			{
			fprintf(stderr, "Cannot read header \n");
			return EXIT_FAILURE;
			}


	samfile_t *out_file = samopen((filename_out!=NULL? filename_out : "-"), "wb", header ); 
	
		if (out_file == NULL) 
			{
			fprintf(stderr,"Failed to open output file . %s.\n",strerror(errno));
			return EXIT_FAILURE;
			} 
		
			

	void *iter = sam_header_parse2(header->text);
	while ( (iter = sam_header2key_val(iter, "RG","ID","SM",&rgId,&sampleName) )!=NULL)
		{
			Sample* searchSample = NULL;
			
			/* sample must be unique */
			
			for (i=0;i<sample_count;++i)
				{ 
				if (strcmp (samples[i].sample_name, sampleName) == 0)
					{
					searchSample = &samples[i];
					break;
					}
				}
				
			if (searchSample == NULL ) 
				{ 					
				samples = (Sample*)realloc(samples,sizeof(Sample)*(sample_count+1));
				VERIFY_NOT_NULL(samples);
				samples[sample_count].sample_name = strdup(sampleName);
				VERIFY_NOT_NULL(samples[sample_count].sample_name);
				samples[sample_count].unMap=0;
				
				searchSample=&samples[sample_count];
				
				sample_count++;
				}

			 /* group must be unique */							
			
			for (i=0;i<group_count;++i) { 
				if (strcmp (group[i].rgId,rgId) == 0) {
					fprintf(stderr,"DUP GROUP");
					return EXIT_FAILURE;
				}
			}
										
			group = (Group*)realloc(group,sizeof(Group)*(group_count+1));	
			VERIFY_NOT_NULL(group);	
			group[group_count].rgId = strdup(rgId);
			VERIFY_NOT_NULL(group[group_count].rgId);
			
			group[group_count].sample = searchSample;  
			group_count++;							
		}
		

	if ((group_count == 0)  && (sample_count ==0))
		{
		fprintf(stderr,"Error, missing SAM header or @RG tag\n");
		return EXIT_FAILURE;
		}


	qsort( group, group_count, sizeof(Group), compareGroup);


	idx = bwa_idx_load(ref, BWA_IDX_ALL);
		if (NULL == idx) {
			fprintf(stderr, "Index load failed %s.\n",strerror(errno));
			exit(EXIT_FAILURE);
		}
		



	while(sam_read1(fp, header, b) >= 0)
	{ 
		nReads++;
		if ( (b->core.flag & BAM_FUNMAP ) )
			{
			Group key;
			search = bam_aux_get(b, "RG");
			if (search == NULL) {
				fprintf(stderr, "Error, missing @RG tag\n");
				return EXIT_FAILURE;
			}else key.rgId = bam_aux2Z(search); 
			
			struct Group *res;
			res = bsearch(
				&key,
				group,
				group_count,
				sizeof(Group),
				compareGroup
				);
			if (res == NULL) {
				return EXIT_FAILURE;
			} else 
				{
				res->sample->unMap++; // Count unmapped reads by sample
				}
						
			fastq = (Fastq*)realloc(fastq,sizeof(Fastq)*(fastq_count+1));	
			VERIFY_NOT_NULL(fastq);	
			

			// Write sequence
			qlen = (b->core.l_qseq);
			if (qlen == 0) {
				fprintf(stderr, "Error, missing sequence\n");
				return EXIT_FAILURE;
			}
       			buf=calloc((qlen+1),sizeof(int8_t));       			
       			buf[qlen] = '\0';
       			seq = bam_get_seq(b);
        		for (i = 0; i < qlen; ++i){
           			buf[i] = bam_seqi(seq, i);
           			buf[i] = seq_nt16_str[buf[i]];
           			}

			fastq[fastq_count].seq = strdup((char*)buf);
			VERIFY_NOT_NULL(fastq[fastq_count].seq);
			

			// Write read name
			qname = strdup(bam_get_qname(b));
			if (qname == NULL) {
				fprintf(stderr, "Error, missing read name\n");
				return EXIT_FAILURE;
			}
			if ((b->core.flag & BAM_FREAD1) && !(b->core.flag & BAM_FREAD2)) read="/1";
         		else if ((b->core.flag & BAM_FREAD2) && !(b->core.flag & BAM_FREAD1)) read="/2";
           		
           		fastq[fastq_count].qname = qname;
           		VERIFY_NOT_NULL(fastq[fastq_count].qname);
           		           		
           		fastq[fastq_count].read = read;
           		VERIFY_NOT_NULL(fastq[fastq_count].read);
           		           		
           		
           		// Write quality	
           		qual = bam_get_qual(b);	
           		if (qual == NULL) {
				fprintf(stderr, "Error, missing quality\n");
				return EXIT_FAILURE;
			}
           		buf=calloc((qlen+1),sizeof(int8_t));       			
       			buf[qlen] = '\0';
       			for (i = 0; i < qlen; ++i) buf[i] = 33 + qual[i];


           		fastq[fastq_count].qual = strdup((char*)buf);
           		VERIFY_NOT_NULL(fastq[fastq_count].qual);
           		
			res->sample->fastq = fastq;
			
			free(buf);
			fastq_count++;	

			if(fastq_count == 1000)  // Write fastq 
				{
				DUMP_FASTQ;
				ALIGN_CONTAMINANTS;
				DEBUG;
				fastq_count=0;
				}		
			}
		samwrite(out_file, b); // Write read in the output file
	}
	

	DUMP_FASTQ;
	ALIGN_CONTAMINANTS;
	float pourcent =0;

	qsort( contaminant, count_contaminants, sizeof(Contaminants), compareContaminant);

	// Write the report containing number of unmapped reads by sample
 	file=fopen(output_report,"w"); 
 		if ( file != NULL) 
 		{
 		for(i=0;i< sample_count;++i)
 			{
      			fprintf(file,"%s\t%lu \n",samples[i].sample_name, samples[i].unMap);
      			}
      		for(i=0;i< count_contaminants;++i)
 			{
 			pourcent = (contaminant[i].contaminants_count / nReads)*100;
      			fprintf(file,"%f%% \t (%.0f/%.0f) \t%s\n", pourcent,contaminant[i].contaminants_count,nReads, contaminant[i].c_name);
      			}
      		} else 
      			{
	      	 	fprintf(stderr,"Cannot read file. %s.\n",strerror(errno));
	      	 	return EXIT_FAILURE;
      	 		}     	 		
 	

        
        // Close files, free and return
       	for (i=0; i<count_contaminants; i++)
		{
		free(contaminant[i].c_name);
		}
	free(contaminant);

	bwa_idx_destroy(idx);
        fclose(file);
	bam_destroy1(b);
	sam_close(fp);
	samclose(out_file);
	free(filename_out);
	free(ref);
	if (file_fastq != NULL) fclose(file_fastq);

	for (i=0; i<group_count; i++)
		{
		free(group[i].rgId);
		}
	free(group);

	
	for (i=0; i<sample_count; i++)
		{
		free(samples[i].sample_name);
		}
	free(samples);
	



	
	
	

	
return EXIT_SUCCESS;
}
        
        
