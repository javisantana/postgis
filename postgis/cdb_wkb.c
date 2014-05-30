#include "liblwgeom_internal.h"
#include "lwgeom_log.h"

#define CDB_WKB_TYPE_SIZE 1
#define CDB_COORD_SIZE sizeof(float) 

static size_t lwgeom_to_cdb_wkb_size(const LWGEOM *geom);
static uint8_t* lwgeom_to_cdb_wkb_buf(const LWGEOM *geom, uint8_t *buf);

static size_t ptarray_to_cdb_wkb_size(const POINTARRAY *pa, uint8_t variant)
{
    int dims = 2;
    size_t size = 0;

    /* Include the npoints if it's not a POINT type) */
    if ( ! ( variant & WKB_NO_NPOINTS ) )
        size += WKB_INT_SIZE;

    /* size of the double list */
    size += pa->npoints * dims * CDB_COORD_SIZE;

    return size;
}

static size_t lwline_to_cdb_wkb_size(const LWLINE *line)
{
    /* ype number */
    size_t size = CDB_WKB_TYPE_SIZE;

    /* Size of point array */
    size += ptarray_to_cdb_wkb_size(line->points, 0);
    return size;
}

static size_t lwpoly_to_cdb_wkb_size(const LWPOLY *poly)
{
    /* type number + number of rings */
    size_t size = CDB_WKB_TYPE_SIZE + WKB_INT_SIZE;
    int i = 0;

    for ( i = 0; i < poly->nrings; i++ )
    {
        /* Size of ring point array */
        size += ptarray_to_cdb_wkb_size(poly->rings[i], 0);
    }

    return size;
}

static size_t lwcollection_to_cdb_wkb_size(const LWCOLLECTION *col)
{
    /* Endian flag + type number + number of subgeoms */
    size_t size = CDB_WKB_TYPE_SIZE + WKB_INT_SIZE;
    int i = 0;

    for ( i = 0; i < col->ngeoms; i++ )
    {
        /* size of subgeom */
        size += lwgeom_to_cdb_wkb_size((LWGEOM*)col->geoms[i]);
    }

    return size;
}



/*
* Empty
* REVIEW
*/
static size_t empty_to_cdb_wkb_size(const LWGEOM *geom)
{
    /* header size + npoints */
    return CDB_WKB_TYPE_SIZE + WKB_INT_SIZE;
}

/*
* POINT
*/
static size_t lwpoint_to_cdb_wkb_size(const LWPOINT *pt)
{
    /* type number */
    size_t size = CDB_WKB_TYPE_SIZE;

    /* Points */
    size += ptarray_to_cdb_wkb_size(pt->point, WKB_NO_NPOINTS);
    return size;
}

static uint8_t* float_to_cdb_wkb_buf(const float d, uint8_t *buf)
{
    char *dptr = (char*)(&d);
    memcpy(buf, dptr, CDB_COORD_SIZE);
    return buf + CDB_COORD_SIZE;
}

static uint8_t* uint8_to_cdb_wkb_buf(uint8_t ival, uint8_t *buf) {
    *buf = ival;
    return buf + 1;
}

static uint8_t* integer_to_cdb_wkb_buf(const int ival, uint8_t *buf) {
    char *iptr = (char*)(&ival);
    memcpy(buf, iptr, WKB_INT_SIZE);
    return buf + WKB_INT_SIZE;
}

/* cdb_wkb_type = wkb_type + 1 */
static uint8_t lwgeom_cdb_wkb_type(const LWGEOM *geom) {
    uint8_t wkb_type = 0;

    switch ( geom->type )
    {
    case POINTTYPE:
        wkb_type = WKB_POINT_TYPE;
        break;
    case LINETYPE:
        wkb_type = WKB_LINESTRING_TYPE;
        break;
    case POLYGONTYPE:
        wkb_type = WKB_POLYGON_TYPE;
        break;
    case MULTIPOINTTYPE:
        wkb_type = WKB_MULTIPOINT_TYPE;
        break;
    case MULTILINETYPE:
        wkb_type = WKB_MULTILINESTRING_TYPE;
        break;
    case MULTIPOLYGONTYPE:
        wkb_type = WKB_MULTIPOLYGON_TYPE;
        break;
    default:
        lwerror("Unsupported geometry type: [%d]", geom->type);
    }
    return wkb_type;
}

static uint8_t* empty_to_cdb_wkb_buf(const LWGEOM *geom, uint8_t *buf)
{
    uint8_t wkb_type = lwgeom_cdb_wkb_type(geom);

    if ( geom->type == POINTTYPE )
    {
        /* Change POINT to MULTIPOINT */
        wkb_type &= ~WKB_POINT_TYPE;     /* clear POINT flag */
        wkb_type |= WKB_MULTIPOINT_TYPE; /* set MULTIPOINT flag */
    }

    /* Set the geometry type */
    buf = uint8_to_cdb_wkb_buf(wkb_type, buf);

    /* Set nrings/npoints/ngeoms to zero */
    buf = integer_to_cdb_wkb_buf(0, buf);
    return buf;
}

static uint8_t* ptarray_to_cdb_wkb_buf(const POINTARRAY *pa, uint8_t *buf, uint8_t variant) {
    int dims = 2;
    int pa_dims = FLAGS_NDIMS(pa->flags);
    int i, j;
    double *dbl_ptr;

    /* Set the number of points (if it's not a POINT type) */
    if ( ! ( variant & WKB_NO_NPOINTS ) )
        buf = integer_to_cdb_wkb_buf(pa->npoints, buf);

    /* Copy coordinates one-by-one otherwise */
    for ( i = 0; i < pa->npoints; i++ )
    {
        LWDEBUGF(4, "Writing point #%d", i);
        dbl_ptr = (double*)getPoint_internal(pa, i);
        for ( j = 0; j < dims; j++ )
        {
            LWDEBUGF(4, "Writing dimension #%d (buf = %p)", j, buf);
            buf = float_to_cdb_wkb_buf(dbl_ptr[j], buf);
        }
    }
    LWDEBUGF(4, "Done (buf = %p)", buf);
    return buf;
}

static uint8_t* lwpoint_to_cdb_wkb_buf(const LWPOINT *pt, uint8_t *buf)
{
    /* Set the endian flag */
    LWDEBUGF(4, "Entering function, buf = %p", buf);

    /* Set the geometry type */
    buf = uint8_to_cdb_wkb_buf(lwgeom_cdb_wkb_type((LWGEOM*)pt), buf);
    LWDEBUGF(4, "Type set, buf = %p", buf);
    /* Set the coordinates */
    buf = ptarray_to_cdb_wkb_buf(pt->point, buf, WKB_NO_NPOINTS);
    LWDEBUGF(4, "Pointarray set, buf = %p", buf);
    return buf;
}

static uint8_t* lwline_to_cdb_wkb_buf(const LWLINE *line, uint8_t *buf)
{
    /* Set the geometry type */
    buf = uint8_to_cdb_wkb_buf(lwgeom_cdb_wkb_type((LWGEOM*)line), buf);
    /* Set the coordinates */
    buf = ptarray_to_cdb_wkb_buf(line->points, buf, 0);
    return buf;
}

static uint8_t* lwpoly_to_cdb_wkb_buf(const LWPOLY *poly, uint8_t *buf)
{
    int i;

    /* Set the geometry type */
    buf = uint8_to_cdb_wkb_buf(lwgeom_cdb_wkb_type((LWGEOM*)poly), buf);

    /* Set the number of rings */
    buf = integer_to_cdb_wkb_buf(poly->nrings, buf);

    for ( i = 0; i < poly->nrings; i++ )
    {
        buf = ptarray_to_cdb_wkb_buf(poly->rings[i], buf, 0);
    }

    return buf;
}

static uint8_t* lwcollection_to_cdb_wkb_buf(const LWCOLLECTION *col, uint8_t *buf)
{
    int i;

    /* Set the geometry type */
    buf = uint8_to_cdb_wkb_buf(lwgeom_cdb_wkb_type((LWGEOM*)col), buf);

    /* Set the number of sub-geometries */
    buf = integer_to_cdb_wkb_buf(col->ngeoms, buf);

    /* Write the sub-geometries. Sub-geometries do not get SRIDs, they
       inherit from their parents. */
    for ( i = 0; i < col->ngeoms; i++ )
    {
        buf = lwgeom_to_cdb_wkb_buf(col->geoms[i], buf);
    }

    return buf;
}



static uint8_t* lwgeom_to_cdb_wkb_buf(const LWGEOM *geom, uint8_t *buf)
{

    if ( lwgeom_is_empty(geom) )
        return empty_to_cdb_wkb_buf(geom, buf);

    switch ( geom->type )
    {
        case POINTTYPE:
            return lwpoint_to_cdb_wkb_buf((LWPOINT*)geom, buf);

        /* LineString and CircularString both have 'points' elements */
        case LINETYPE:
            return lwline_to_cdb_wkb_buf((LWLINE*)geom, buf);

        /* Polygon has 'nrings' and 'rings' elements */
        case POLYGONTYPE:
            return lwpoly_to_cdb_wkb_buf((LWPOLY*)geom, buf);

        case MULTIPOINTTYPE:
        case MULTILINETYPE:
        case MULTIPOLYGONTYPE:
            return lwcollection_to_cdb_wkb_buf((LWCOLLECTION*)geom, buf);

        /* Unknown type! */
        default:
            lwerror("Unsupported geometry type: %s [%d]", lwtype_name(geom->type), geom->type);
    }
    /* Return value to keep compiler happy. */
    return 0;
}


/*
* GEOMETRY
*/
static size_t lwgeom_to_cdb_wkb_size(const LWGEOM *geom)
{
    size_t size = 0;

    if ( geom == NULL )
        return 0;

    /* Short circuit out empty geometries */
    if ( lwgeom_is_empty(geom) )
    {
        return empty_to_cdb_wkb_size(geom);
    }

    switch ( geom->type )
    {
        case POINTTYPE:
            size += lwpoint_to_cdb_wkb_size((LWPOINT*)geom);
            break;

        case CIRCSTRINGTYPE:
        case LINETYPE:
            size += lwline_to_cdb_wkb_size((LWLINE*)geom);
            break;

        /* Polygon has nrings and rings elements */
        case POLYGONTYPE:
            size += lwpoly_to_cdb_wkb_size((LWPOLY*)geom);
            break;


        /* All these Collection types have ngeoms and geoms elements */
        case MULTIPOINTTYPE:
        case MULTILINETYPE:
        case MULTIPOLYGONTYPE:
            size += lwcollection_to_cdb_wkb_size((LWCOLLECTION*)geom);
            break;

        /* Unknown type! */
        default:
            lwerror("Unsupported geometry type: %s [%d]", lwtype_name(geom->type), geom->type);
    }

    return size;
}


uint8_t* lwgeom_to_cdb_wkb(const LWGEOM *geom, uint8_t variant, size_t *size_out)
{
    size_t buf_size;
    uint8_t *buf = NULL;
    uint8_t *wkb_out = NULL;

    /* Initialize output size */
    if ( size_out ) *size_out = 0;

    if ( geom == NULL )
    {
        LWDEBUG(4,"Cannot convert NULL into WKB.");
        lwerror("Cannot convert NULL into WKB.");
        return NULL;
    }

    /* Calculate the required size of the output buffer */
    buf_size = lwgeom_to_cdb_wkb_size(geom);
    LWDEBUGF(4, "WKB output size: %d", buf_size);

    if ( buf_size == 0 )
    {
        LWDEBUG(4,"Error calculating output WKB buffer size.");
        lwerror("Error calculating output WKB buffer size.");
        return NULL;
    }

    /* Allocate the buffer */
    buf = lwalloc(buf_size);

    if ( buf == NULL )
    {
        LWDEBUGF(4,"Unable to allocate %d bytes for WKB output buffer.", buf_size);
        lwerror("Unable to allocate %d bytes for WKB output buffer.", buf_size);
        return NULL;
    }

    /* Retain a pointer to the front of the buffer for later */
    wkb_out = buf;

    /* Write the WKB into the output buffer */
    buf = lwgeom_to_cdb_wkb_buf(geom, buf);

    LWDEBUGF(4,"buf (%p) - wkb_out (%p) = %d", buf, wkb_out, buf - wkb_out);

    /* The buffer pointer should now land at the end of the allocated buffer space. Let's check. */
    if ( buf_size != (buf - wkb_out) )
    {
        LWDEBUG(4,"Output WKB is not the same size as the allocated buffer.");
        lwerror("Output WKB is not the same size as the allocated buffer.");
        lwfree(wkb_out);
        return NULL;
    }

    /* Report output size */
    if ( size_out ) *size_out = buf_size;

    return wkb_out;
}



/** convert LWGEOM to wkb (in BINARY format) */
PG_FUNCTION_INFO_V1(LWGEOM_CDB_asBinary);
Datum LWGEOM_CDB_asBinary(PG_FUNCTION_ARGS)
{
    GSERIALIZED *geom;
    LWGEOM *lwgeom;
    uint8_t *wkb;
    size_t wkb_size;
    bytea *result;
    uint8_t variant = WKB_ISO;

    /* Get a 2D version of the geometry */
    geom = (GSERIALIZED*)PG_DETOAST_DATUM(PG_GETARG_DATUM(0));
    lwgeom = lwgeom_from_gserialized(geom);

    /* Write to WKB and free the geometry */
    wkb = lwgeom_to_cdb_wkb(lwgeom, variant, &wkb_size);
    lwgeom_free(lwgeom);

    /* Write to text and free the WKT */
    result = palloc(wkb_size + VARHDRSZ);
    memcpy(VARDATA(result), wkb, wkb_size);
    SET_VARSIZE(result, wkb_size + VARHDRSZ);
    pfree(wkb);

    /* Return the text */
    PG_FREE_IF_COPY(geom, 0);
    PG_RETURN_BYTEA_P(result);
}

