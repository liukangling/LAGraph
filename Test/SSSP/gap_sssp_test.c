//------------------------------------------------------------------------------
// sssp_test: read in a matrix and test delta stepping sssp
//------------------------------------------------------------------------------

/*
    LAGraph:  graph algorithms based on GraphBLAS

    Copyright 2019 LAGraph Contributors.

    (see Contributors.txt for a full list of Contributors; see
    ContributionInstructions.txt for information on how you can Contribute to
    this project).

    All Rights Reserved.

    NO WARRANTY. THIS MATERIAL IS FURNISHED ON AN "AS-IS" BASIS. THE LAGRAPH
    CONTRIBUTORS MAKE NO WARRANTIES OF ANY KIND, EITHER EXPRESSED OR IMPLIED,
    AS TO ANY MATTER INCLUDING, BUT NOT LIMITED TO, WARRANTY OF FITNESS FOR
    PURPOSE OR MERCHANTABILITY, EXCLUSIVITY, OR RESULTS OBTAINED FROM USE OF
    THE MATERIAL. THE CONTRIBUTORS DO NOT MAKE ANY WARRANTY OF ANY KIND WITH
    RESPECT TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.

    Released under a BSD license, please see the LICENSE file distributed with
    this Software or contact permission@sei.cmu.edu for full terms.

    Created, in part, with funding and support from the United States
    Government.  (see Acknowledgments.txt file).

    This program includes and/or can make use of certain third party source
    code, object code, documentation and other files ("Third Party Software").
    See LICENSE file for more details.

*/

//------------------------------------------------------------------------------

// Contributed by Jinhao Chen, Scott Kolodziej and Tim Davis, Texas A&M
// University

// usage:
// sssp_test < in.mtx > out
// in.mtx is the Matrix Market file, out is the level set.

// sssp_test in.mtx delta source.mtx > out
// sssp_test in.grb delta source.mtx > out

#include "sssp_test.h"

#define NTHREAD_LIST 1
#define THREAD_LIST 0

#define LAGRAPH_FREE_ALL            \
{                                   \
    GrB_free (&A_in);               \
    GrB_free (&A);                  \
    GrB_free (&SourceNodes) ;       \
    LAGRAPH_FREE (I);               \
    LAGRAPH_FREE (J);               \
    LAGRAPH_FREE (W);               \
    LAGRAPH_FREE (d);               \
    LAGRAPH_FREE (pi);              \
    GrB_free (&path_lengths);       \
    GrB_free (&path_lengths1);      \
}

int main (int argc, char **argv)
{
    GrB_Info info ;

    GrB_Matrix A_in = NULL ;
    GrB_Matrix A = NULL ;
    GrB_Matrix SourceNodes = NULL ;
    GrB_Index *I = NULL, *J = NULL ;    // for col/row indices of entries from A
    int32_t *W = NULL ;
    int32_t *d = NULL ;                 // for BF result
    int64_t *pi = NULL ;
    GrB_Vector path_lengths = NULL ;
    GrB_Vector path_lengths1 = NULL ;
    LAGRAPH_OK (LAGraph_init ( )) ;

    GrB_Index ignore, s = 0 ;
    int32_t delta = 3;

    //GxB_set(GxB_CHUNK, 4096) ;
    // LAGraph_set_nthreads (1) ;

    bool test_pass = true;
    double tic[2];

    int nt = NTHREAD_LIST ;
    int Nthreads [20] = { 0, THREAD_LIST } ;
    int nthreads_max = LAGraph_get_nthreads ( ) ;
    if (Nthreads [1] == 0)
    {
        // create thread list automatically
        Nthreads [1] = nthreads_max ;
        for (int t = 2 ; t <= nt ; t++)
        {
            Nthreads [t] = Nthreads [t-1] / 2 ;
            if (Nthreads [t] == 0) nt = t-1 ;
        }
    }
    printf ("threads to test: ") ;
    for (int t = 1 ; t <= nt ; t++)
    {
        int nthreads = Nthreads [t] ;
        if (nthreads > nthreads_max) continue ;
        printf (" %d", nthreads) ;
    }
    printf ("\n") ;

    LAGraph_tic (tic);

    int batch_size = 4 ;

    //--------------------------------------------------------------------------
    // get the matrix
    //--------------------------------------------------------------------------

    char *matrix_name = (argc > 1) ? argv [1] : "stdin" ;

    if (argc > 1)
    {

        //----------------------------------------------------------------------
        // Usage: ./sssp_test matrixfile.mtx ...
        //----------------------------------------------------------------------

        // read in the file in Matrix Market format from the input file
        char *filename = argv [1] ;
        printf ("matrix: %s\n", filename) ;

        // find the filename extension
        size_t len = strlen (filename) ;
        char *ext = NULL ;

        for (int k = len-1 ; k >= 0 ; k--)
        {
            if (filename [k] == '.')
            {
                ext = filename + k ;
                printf ("[%s]\n", ext) ;
                break ;
            }
        }
        bool is_binary = (ext != NULL && strncmp (ext, ".grb", 4) == 0) ;

        if (is_binary)
        {
            printf ("Reading binary file: %s\n", filename) ;
            LAGRAPH_OK (LAGraph_binread (&A_in, filename)) ;
        }
        else
        {
            printf ("Reading Matrix Market file: %s\n", filename) ;
            FILE *f = fopen (filename, "r") ;
            if (f == NULL)
            {
                printf ("Matrix file not found: [%s]\n", filename) ;
                exit (1) ;
            }
            LAGRAPH_OK (LAGraph_mmread(&A_in, f));
            fclose (f) ;
        }
    }
    else
    {

        //----------------------------------------------------------------------
        // Usage:  ./sssp_test < matrixfile.mtx
        //----------------------------------------------------------------------

        printf ("matrix: from stdin\n") ;

        // read in the file in Matrix Market format from stdin
        LAGRAPH_OK (LAGraph_mmread(&A_in, stdin));
    }

    // get the size of the problem.
    GrB_Index anz ;
    GrB_Matrix_nvals (&anz, A_in);
    GrB_Index nrows, ncols;
    LAGr_Matrix_nrows(&nrows, A_in);
    LAGr_Matrix_ncols(&ncols, A_in);
    GrB_Index n = nrows;

    //--------------------------------------------------------------------------
    // get delta
    //--------------------------------------------------------------------------

    if (argc > 2)
    {
        // usage:  ./sssp_test matrix delta ...
        delta = atoi (argv [2]) ;
    }
    else
    {
        // usage:  ./sssp_test matrix
        // or:     ./sssp_test < matrix
        delta = 2 ;
    }
    printf ("delta: %d\n", delta) ;

    //--------------------------------------------------------------------------
    // get the source nodes
    //--------------------------------------------------------------------------

    if (argc > 3)
    {

        //----------------------------------------------------------------------
        // usage:  ./sssp_test matrix delta sourcenodes
        //----------------------------------------------------------------------

        // read in source nodes in Matrix Market format from the input file
        char *filename = argv [3] ;
        printf ("sources: %s\n", filename) ;
        FILE *f = fopen (filename, "r") ;
        if (f == NULL)
        {
            printf ("Source node file not found: [%s]\n", filename) ;
            exit (1) ;
        }
        LAGRAPH_OK (LAGraph_mmread (&SourceNodes, f)) ;
        fclose (f) ;

    }
    else
    {

        //----------------------------------------------------------------------
        // create random source nodes
        //----------------------------------------------------------------------

        #define NSOURCES 64

        LAGRAPH_OK (GrB_Matrix_new (&SourceNodes, GrB_INT64, NSOURCES, 1)) ;
        srand (1) ;
        for (int k = 0 ; k < NSOURCES ; k++)
        {
            int64_t i = 1 + rand ( ) % n ;      // 1-based source node
            // SourceNodes [k] = i 
            LAGRAPH_OK (GrB_Matrix_setElement (SourceNodes, i, k, 0)) ;
        }
        GrB_Matrix_nvals (&ignore, SourceNodes);
    }

    double t_read = LAGraph_toc (tic) ;
    printf ("read time: %g sec\n", t_read) ;

    //--------------------------------------------------------------------------

    // convert input matrix to INT32
    GrB_Type type ;
    GxB_Matrix_type (&type, A_in) ;
    if (type == GrB_INT32)
    {
        A = A_in ;
        A_in = NULL ;
    }
    else
    {
        GrB_Matrix_new (&A, GrB_INT32, n, n) ;
        GrB_apply (A, NULL, NULL, GrB_IDENTITY_INT32, A_in, NULL) ;
        GrB_free (&A_in) ;
    }

    // get the number of source nodes
    GrB_Index nsource;
    LAGRAPH_OK (GrB_Matrix_nrows (&nsource, SourceNodes));
    LAGr_Matrix_nvals (&ignore, SourceNodes);

    // try converting to column format (this is slower than the default)
    // GxB_set (A, GxB_FORMAT, GxB_BY_COL) ;

    GxB_fprint (A, 2, stdout) ;
    // GxB_fprint (SourceNodes, GxB_COMPLETE, stdout) ;

    #if 1
    {
        char outfile [256] ;
        sprintf (outfile, "sources_%ld.mtx", n) ;
        FILE *f = fopen (outfile, "w") ;
        LAGraph_mmwrite ((GrB_Matrix) SourceNodes, f) ;
        fclose (f) ;
    }
    #endif

    //--------------------------------------------------------------------------
    // Begin tests
    //--------------------------------------------------------------------------

    int nthreads = LAGraph_get_nthreads();
    printf ("input graph: nodes: %"PRIu64" edges: %"PRIu64" max nthreads %d\n",
        n, anz, nthreads) ;
    printf ("# of source nodes: %lu\n", nsource) ;

    int ntrials = (int) nsource ;

for (int tt = 1 ; tt <= nt ; tt++)
{
    int nthreads = Nthreads [tt] ;
    if (nthreads > nthreads_max) continue ;

//  if (nthreads == 64) { printf ("SSSP: 64 threads; skipped\n") ; continue ; }

    LAGraph_set_nthreads (nthreads) ;

    double total_time1 = 0 ;
    double total_time2 = 0 ;
    double total_time3 = 0 ;
    double total_time31 = 0 ;
    double total_time32 = 0 ;
    double total_time_sssp12 = 0 ;
    double total_time_sssp12c = 0 ;
    double t1, t2, t3;

    for (int trial = 0 ; trial < ntrials ; trial++)
    {

        //----------------------------------------------------------------------
        // get the source node for this trial
        //----------------------------------------------------------------------

        s = -1 ;
        LAGRAPH_OK (GrB_Matrix_extractElement (&s, SourceNodes, trial, 0));
        // convert from 1-based to 0-based
        s-- ;
        // printf ("\nTrial %d : source node: %"PRIu64"\n", trial, s) ;
        printf ("\n") ;

        //----------------------------------------------------------------------
        // sssp
        //----------------------------------------------------------------------

#if 0

        printf(" - Start Test: delta-stepping Single Source Shortest Paths"
            " (apply operator)\n");

        // start the timer
        LAGraph_tic (tic) ;

        GrB_free (&path_lengths);
        LAGRAPH_OK (LAGraph_sssp (&path_lengths, A, s, delta)) ;

        // stop the timer
        t2 = LAGraph_toc (tic) ;
        printf ("SSSP (apply)    time: %12.6g (sec), rate:"
            " %12.6g (1e6 edges/sec)\n", t2, 1e-6*((double) anz) / t2) ;

        total_time2 += t2;

#endif

        //----------------------------------------------------------------------
        // sssp1
        //----------------------------------------------------------------------

#if 0
        // Start the timer
        LAGraph_tic (tic);

        GrB_free (&path_lengths1);
        LAGRAPH_OK (LAGraph_sssp1 (&path_lengths1, A, s, delta)) ;

        // Stop the timer
        t3 = LAGraph_toc (tic);
        printf ("SSSP1 (select)  time: %12.6g (sec), rate:"
            " %12.6g (1e6 edges/sec)\n", t3, 1e-6*((double) anz) / t3) ;

        total_time3 += t3;
#endif

        //----------------------------------------------------------------------
        // sssp11a
        //----------------------------------------------------------------------

#if 0
        // Start the timer
        LAGraph_tic (tic);
        GrB_free (&path_lengths1);
        LAGRAPH_OK (LAGraph_sssp11a(&path_lengths1, A, s, delta, true)) ;

        // Stop the timer
        t3 = LAGraph_toc (tic);
        printf ("SSSP11a(select)  time: %12.6g (sec), rate:"
            " %12.6g (1e6 edges/sec)\n", t3, 1e-6*((double) anz) / t3) ;
#endif

        //----------------------------------------------------------------------
        // sssp11b
        //----------------------------------------------------------------------

#if 0
        // Start the timer
        LAGraph_tic (tic);

        GrB_free (&path_lengths1);
        LAGRAPH_OK (LAGraph_sssp11b (&path_lengths1, A, s, delta, true)) ;

        // Stop the timer
        t3 = LAGraph_toc (tic);
        printf ("SSSP11b (select)  time: %12.6g (sec), rate:"
            " %12.6g (1e6 edges/sec)\n", t3, 1e-6*((double) anz) / t3) ;
#endif

        //----------------------------------------------------------------------
        // sssp11: with dense vector t
        //----------------------------------------------------------------------

#if 0
        printf ("\n---------------------- sssp11: nthreads %d\n", nthreads) ;

        // Start the timer
        LAGraph_tic (tic);
        GrB_free (&path_lengths1);
        LAGRAPH_OK (LAGraph_sssp11 (&path_lengths1, A, s, delta, true)) ;

        // Stop the timer
        t3 = LAGraph_toc (tic);
        //printf ("SSSP11 (select)  time: %12.6g (sec), rate:"
        //    " %12.6g (1e6 edges/sec)\n", t3, 1e-6*((double) anz) / t3) ;
        total_time31 += t3;

        printf ("\n sssp11: done\n\n") ;

        #if 0
        // save the results
        if (tt == 1)
        {
            char savefilename [2000] ;
            sprintf (savefilename, "pathlen_%lu_sssp11.mtx", n) ;
            FILE *savefile = fopen (savefilename, "w") ;
            LAGraph_mmwrite ((GrB_Matrix) path_lengths1, savefile) ;
            fclose (savefile) ;
        }
        #endif
#endif

        //----------------------------------------------------------------------
        // sssp12: with dense vector t
        //----------------------------------------------------------------------

//      printf ("\n----sssp12: nthreads %d trial: %d source: %lu (0-based)\n",
//          nthreads, trial, s) ;

#if 1
        // Start the timer
        LAGraph_tic (tic) ;
        GrB_free (&path_lengths1) ;
        LAGRAPH_OK (LAGraph_sssp12 (&path_lengths1, A, s, delta, true)) ;

        // Stop the timer
        t3 = LAGraph_toc (tic) ;
        printf ("sssp12 : threads: %2d trial: %2d source %9lu "
            "time: %10.4f sec\n", nthreads, trial, s, t3) ;
        total_time_sssp12 += t3 ;
#endif

        //----------------------------------------------------------------------
        // sssp12c: with dense vector t
        //----------------------------------------------------------------------

//      printf ("\n----sssp12c: nthreads %d trial: %d source: %lu (0-based)\n",
//          nthreads, trial, s) ;

        // Start the timer
        LAGraph_tic (tic) ;
        GrB_free (&path_lengths1) ;
        LAGRAPH_OK (LAGraph_sssp12c (&path_lengths1, A, s, delta, true)) ;

        // Stop the timer
        t3 = LAGraph_toc (tic) ;
        printf ("sssp12c: threads: %2d trial: %2d source %9lu "
            "time: %10.4f sec\n", nthreads, trial, s, t3) ;
        total_time_sssp12c += t3 ;

        #if 1
        {
            char outfile [256] ;
            sprintf (outfile, "pathlen_%02d_%ld.mtx", trial, n) ;
            FILE *f = fopen (outfile, "w") ;
            LAGraph_mmwrite ((GrB_Matrix) path_lengths1, f) ;
            fclose (f) ;
        }
        #endif

        #if 0
        // save the results
        if (tt == 1)
        {
            char savefilename [2000] ;
            sprintf (savefilename, "pathlen_%lu_sssp12.mtx", n) ;
            FILE *savefile = fopen (savefilename, "w") ;
            LAGraph_mmwrite ((GrB_Matrix) path_lengths1, savefile) ;
            fclose (savefile) ;
        }
        #endif

        //----------------------------------------------------------------------
        // sssp2
        //----------------------------------------------------------------------

#if 0
        // Start the timer
        LAGraph_tic (tic);

        GrB_free (&path_lengths1);
        LAGRAPH_OK (LAGraph_sssp2 (&path_lengths1, A, s, delta)) ;

        // Stop the timer
        t3 = LAGraph_toc (tic);
        //printf ("SSSP2 (select)  time: %12.6g (sec), rate:"
        //    " %12.6g (1e6 edges/sec)\n", t3, 1e-6*((double) anz) / t3) ;
        total_time32 += t3;
#endif

        //----------------------------------------------------------------------
        // find shortest path using BF on node s with LAGraph_pure_c
        //----------------------------------------------------------------------

        // get the triplet form for the Bellman-Ford function
        if (tt == 1)
        {
#if 0
            LAGr_Matrix_nvals (&anz, A) ;
            I = LAGraph_malloc (anz, sizeof(GrB_Index)) ;
            J = LAGraph_malloc (anz, sizeof(GrB_Index)) ;
            W = LAGraph_malloc (anz, sizeof(int32_t)) ;
            if (I == NULL || J == NULL || W == NULL)
            {
                LAGRAPH_ERROR ("out of memory", GrB_OUT_OF_MEMORY) ;
            }
            LAGRAPH_OK (GrB_Matrix_extractTuples_INT32(I, J, W, &anz, A));

            printf ("BF source: %lu (zero-based)\n", s) ;

            // start the timer
            LAGraph_tic (tic) ;

            LAGRAPH_FREE (d) ;
            LAGRAPH_FREE (pi) ;
            LAGRAPH_OK (LAGraph_BF_pure_c (&d, &pi, s, n, anz, I, J, W)) ;

            // stop the timer
            t1 = LAGraph_toc (tic) ;
            // printf ("BF_pure_c       time: %12.6g (sec), rate:"
            //     " %g (1e6 edges/sec)\n", t1, 1e-6*((double) anz) / t1) ;

            total_time1 += t1;

            LAGRAPH_FREE (pi) ;
            LAGRAPH_FREE (I) ;
            LAGRAPH_FREE (J) ;
            LAGRAPH_FREE (W) ;

    // sample input:

//      %%MatrixMarket matrix coordinate real general
//      %%GraphBLAS GrB_INT32
//      % Matrix from the cover of "Graph Algorithms in the Language of Linear
//      % Algebra", Kepner and Gilbert.  Note that cover shows A'.  This is A.
//      7 7 12
//      4 1 4
//      1 2 2
//      4 3 1
//      6 3 5
//      7 3 9
//      1 4 7
//      7 4 1
//      2 5 5
//      7 5 1
//      3 6 1
//      5 6 7
//      2 7 8

    // sample output:

    //      %%MatrixMarket matrix coordinate integer general
    //      %%GraphBLAS GrB_INT32
    //      %% BF path len for cover.mtx
    //      %% source node: 1 (in 1-based notation)
    //      7 1 7
    //      1 1 0
    //      2 1 2
    //      3 1 8
    //      4 1 7
    //      5 1 7
    //      6 1 9
    //      7 1 10

            #if 0
            // save the results
            if (tt == 1)
            {
                char savefilename [2000] ;
                sprintf (savefilename, "pathlen_%lu_bf.mtx", n) ;
                FILE *savefile = fopen (savefilename, "w") ;
                // LAGraph_mmwrite ((GrB_Matrix) path_lengths1, savefile) ;
                fprintf (savefile, "%%%%MatrixMarket matrix coordinate integer"
                    " general\n%%%%GraphBLAS GrB_INT32\n") ;
                fprintf (savefile, "%%%% BF path len for %s\n", matrix_name) ;
                fprintf (savefile, "%%%% source node: %lu (in 1-based)\n",
                    s+1) ;
                fprintf (savefile, "%lu 1 %lu\n", n, n) ;
                for (int i = 0 ; i < n ; i++)
                {
                    fprintf (savefile, "%d 1 %d\n", i+1, d [i]) ;
                }
                fclose (savefile) ;
            }
            #endif

    #if 0
            // write the result to result file if there is none
            // if( access( fname, F_OK ) == -1 )
            {
                FILE *file = fopen(fname)
                for (int64_t i = 0; i < n; i++)
                {
                    fprintf(file, "%d\n", d[i]);
                }
            }
    #endif

            //------------------------------------------------------------------
            // check the result for correctness
            //------------------------------------------------------------------

    #if 0
            for (int64_t i = 0; i < n; i++)
            {
                bool test_result ;

                #if 0
                double x = INT32_MAX;
                LAGr_Vector_extractElement(&x, path_lengths, i);
                test_result = ((double)d[i] == x);
                test_pass &= test_result;
                if (!test_result)
                {
                    printf ("  Failure at %"PRId64" calculated by sssp\n", i);
                    printf ("  x = %g\n", x);
                    printf ("  d = %d\n", d[i]);
                    printf ("\n") ;
                    abort ( ) ;
                }
                #endif

                int32_t x1 = INT32_MAX;
                LAGr_Vector_extractElement(&x1, path_lengths1, i);
                test_result = (d[i] == x1);
                test_pass &= test_result;
                if (!test_result)
                {
                    printf ("  Failure at %"PRId64" caculated by sssp1\n", i);
                    printf ("  x = %d\n", x1);
                    printf ("  d = %d\n", d[i]);
                    printf ("\n") ;
                    abort ( ) ;
                }
            }
    #endif
#endif

            // free the result from LAGraph_BF_pure_c
            // LAGRAPH_FREE (d) ;
        }

        // HACK
        // break ;
    }

    //--------------------------------------------------------------------------
    // report results
    //--------------------------------------------------------------------------

    printf ("\n") ;

    double e = (double) anz ;

    if (total_time1 > 0)
    {
        total_time1 = total_time1 / ntrials ;
        printf ("%2d: BF      time: %14.6f sec  rate: %8.2f\n",
            nthreads, total_time1, 1e-6 * e / total_time1) ;
        if (n > 1000) LAGr_log (matrix_name, "BF", nthreads, total_time1) ;
    }

    #if 0
    printf ("Average time per trial (apply operator): %g sec\n",
        total_time2 / ntrials);
    #endif

    if (total_time3 > 0)
    {
        total_time3 = total_time3 / ntrials ;
        printf ("%2d: SSSP1   time: %14.6f sec  rate: %8.2f (delta %d)\n",
            nthreads, total_time3, 1e-6 * e / total_time3, delta) ;
        if (n > 1000) LAGr_log (matrix_name, "SSSP1", nthreads, total_time3) ;
    }

    if (total_time31 > 0)
    {
        total_time31 = total_time31 / ntrials ;
        printf ("%2d: SSSP11  time: %14.6f sec  rate: %8.2f (delta %d)\n",
            nthreads, total_time31, 1e-6 * e / total_time31, delta) ;
        if (n > 1000) LAGr_log (matrix_name, "SSSP11", nthreads, total_time31) ;
    }

    if (total_time32 > 0)
    {
        total_time32 = total_time32 / ntrials ;
        printf ("%2d: SSSP2   time: %14.6f sec  rate: %8.2f (delta %d)\n",
            nthreads, total_time32, 1e-6 * e / total_time32, delta) ;
        if (n > 1000) LAGr_log (matrix_name, "SSSP2", nthreads, total_time32) ;
    }

    if (total_time_sssp12 > 0)
    {
        total_time_sssp12 = total_time_sssp12 / ntrials ;
        printf ("%2d: SSSP12  time: %14.6f sec  rate: %8.2f (delta %d)\n",
            nthreads, total_time_sssp12, 1e-6 * e / total_time_sssp12, delta) ;
        if (n > 1000)
        {
            LAGr_log (matrix_name, "SSSP12", nthreads, total_time_sssp12) ;
        }
    }

    if (total_time_sssp12c > 0)
    {
        total_time_sssp12c = total_time_sssp12c / ntrials ;
        printf ("%2d: SSSP12c time: %14.6f sec  rate: %8.2f (delta %d)\n",
            nthreads, total_time_sssp12c, 1e-6 * e / total_time_sssp12c, delta);
        if (n > 1000)
        {
            LAGr_log (matrix_name, "SSSP12c", nthreads, total_time_sssp12c) ;
        }
    }

}

    //--------------------------------------------------------------------------
    // free all workspace and finish
    //--------------------------------------------------------------------------

    LAGRAPH_FREE_ALL;
    LAGRAPH_OK (LAGraph_finalize());

#if 0
    if(!test_pass)
    {
        printf("ERROR! TEST FAILURE\n") ;
    }
    else
    {
        printf("all tests passed\n");
    }
#endif

    return (GrB_SUCCESS);
}

