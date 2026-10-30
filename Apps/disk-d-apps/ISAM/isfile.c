#include "stdio.h"
#include "isamdb.h"

int i_mktbl(int table_name)
{
    int file_descriptor;
    int rc;
    int table_index;
    char filename[20];

    table_index = checked_table(table_name);
    if (table_index < 0)
        return table_index;
    rc = valid_config(&g_cfg);
    if (rc != I_OK)
        return rc;
    invalidate_table_files();
    if (build_pending(table_index))
    {
        build_table_filename(table_index, filename, "BLD");
        if (unlink(filename) != 0)
            return I_EWRIT;
    }
    build_table_filename(table_index, filename, "DAT");
    file_descriptor = open(filename, O_CREAT | O_TRUNC | O_WRONLY, 0);
    if (file_descriptor == ERROR)
        return I_EOPEN;
    close(file_descriptor);
    return pki_create(table_name);
}

#undef close
#define close(file_descriptor) close_table_file(file_descriptor)

static int finish_mutation(int table, int fd,
    struct persistent_metadata *metadata)
{
    int rc;

    metadata->index_count = g_cfg.tbls[table].idxcnt;
    rc = save_table_metadata(table, metadata);
    close(fd);
    return rc;
}

/* Insert record - append to table data file */
int i_insrt(int tblnam, char *rec, int rsiz)
{
    int fd;
    int i, tsz, nbytes;
    int rc;
    int phys;
    int reuse;
    int reuse_phys;
    int next_free_slot;
    long offset;
    char *sbuf;
    struct persistent_metadata metadata;
    struct persistent_metadata new_metadata;
    
    sbuf = record_workspace;
    i = checked_table(tblnam);
    if (i < 0)
        return i;
    rc = load_table_metadata(i, &metadata);
    if (rc != I_OK)
        return rc;
            
            /* Verify record size matches table definition */
            tsz = g_cfg.tbls[i].recsz;
            if (rsiz != tsz)
                return I_ESIZE;
            if (rec[0] == I_DELFLAG)
                return I_ERECORD;
            if (metadata.free_head < 0 && metadata.high_water_slot == 32767)
                return I_EFULL;
            /* Open for read/write */
            fd = open_table_file(i, "DAT", O_RDWR);
            if (fd == ERROR)
                return I_EOPEN;
            rc = prepare_key_changes(i, 0, rec, -1, key_changes);
            if (rc != I_OK)
            {
                close(fd);
                return rc;
            }
            
            nbytes = rsiz;
            if (nbytes > I_RECSZ)
            {
                close(fd);
                return I_EWRIT;
            }
            reuse = metadata.free_head >= 0;
            reuse_phys = metadata.free_head;
            next_free_slot = -1;
            if (reuse && reuse_phys >= 0)
            {
                phys = reuse_phys;
                offset = (long)phys * nbytes;
                if (engine_seek(fd, offset, 0) < 0L)
                {
                    close(fd);
                    return I_EWRIT;
                }
                if (engine_read(fd, sbuf, nbytes) != nbytes ||
                    sbuf[0] != I_DELFLAG || engine_seek(fd, offset, 0) < 0L)
                {
                    close(fd);
                    return I_EINDEX;
                }
                next_free_slot = read_free_link(sbuf);
                if (next_free_slot < -1 || next_free_slot == reuse_phys ||
                    next_free_slot >= metadata.high_water_slot)
                {
                    close(fd);
                    return I_EINDEX;
                }
            }
            else
            {
                phys = metadata.high_water_slot;
                offset = (long)phys * nbytes;
                if (engine_seek(fd, offset, 0) < 0L)
                {
                    close(fd);
                    return I_EWRIT;
                }
            }

            new_metadata = metadata;
            new_metadata.active_count++;
            new_metadata.generation++;
            if (reuse)
                new_metadata.free_head = next_free_slot;
            else
                new_metadata.high_water_slot = phys + 1;
            if (engine_write(fd, rec, nbytes) != nbytes)
            {
                close(fd);
                return I_EWRIT;
            }
            if (fsync(fd) != 0)
            {
                close(fd);
                return I_EWRIT;
            }

            defer_table_file_invalidation = 1;
            rc = apply_key_changes(i, rec, phys, key_changes);
            defer_table_file_invalidation = 0;
            if (rc != I_OK)
            {
                close(fd);
                return rc;
            }
            return finish_mutation(i, fd, &new_metadata);
}

/* Find physical index of Nth logical (non-deleted) record */
#ifndef ISAM_BANK_LIBRARY
static int i_findlog(int tblnam, int logidx, int *physidx)
{
    int fd;
    int i, tsz, nbytes;
    int phys, logical;
    long offset;
    char *sbuf;

    stats_add(&i_runtime_stats.table_scans, 1);
    sbuf = record_workspace;
    i = checked_table(tblnam);
    if (i < 0)
        return i;
            tsz = g_cfg.tbls[i].recsz;
            
            fd = open_table_file(i, "DAT", O_RDONLY);
            if (fd == ERROR)
                return I_EOPEN;
            
            nbytes = tsz;
            if (nbytes > I_RECSZ)
            {
                close(fd);
                return I_EREAD;
            }
            
            logical = 0;
            for (phys = 0; phys < g_cfg.tbls[i].maxrec; phys++)
            {
                stats_add(&i_runtime_stats.scan_slots, 1);
                offset = (long)phys * nbytes;
                if (engine_seek(fd, offset, 0) < 0L)
                {
                    close(fd);
                    return I_EREAD;
                }
                if (engine_read(fd, sbuf, nbytes) != nbytes)
                {
                    close(fd);
                    return I_EREAD;
                }
                
                if (sbuf[0] != I_DELFLAG)
                {
                    if (logical == logidx)
                    {
                        *physidx = phys;
                        close(fd);
                        return I_OK;
                    }
                    logical++;
                }
            }
            
            close(fd);
            return I_ENREC;
}
#endif

static int read_record_slot(int table, int fd, char *rec, int slot)
{
    int size;
    long offset;
    stats_add(&i_runtime_stats.physical_reads, 1);
    if (slot < 0 || slot >= g_cfg.tbls[table].maxrec)
        return I_ENREC;
    size = g_cfg.tbls[table].recsz;
    offset = (long)slot * size;
    if (engine_seek(fd, offset, 0) < 0L || engine_read(fd, rec, size) != size)
        return I_EREAD;
    return rec[0] == I_DELFLAG ? I_ENREC : I_OK;
}

int i_rdphys(int table, char *rec, int slot)
{
    int fd, rc;
    if (checked_table(table) < 0)
        return I_ENTBL;
    fd = open_table_file(table, "DAT", O_RDONLY);
    if (fd == ERROR)
        return I_EOPEN;
    rc = read_record_slot(table, fd, rec, slot);
    close(fd);
    return rc;
}

/* Write record by physical index (bypasses logical scan) */
int i_wrphys(int tblnam, char *rec, int rsiz, int phys)
{
    int fd;
    int i, tsz, nbytes;
    long offset;
    char *oldrec;
    int rc;
    struct persistent_metadata metadata;
    struct persistent_metadata new_metadata;
    
    oldrec = record_workspace;
    if (phys < 0)
        return I_ENREC;
    
    i = checked_table(tblnam);
    if (i < 0)
        return i;
            rc = load_table_metadata(i, &metadata);
            if (rc != I_OK)
                return rc;
            tsz = g_cfg.tbls[i].recsz;
            if (rsiz != tsz)
                return I_ESIZE;
            if (rec[0] == I_DELFLAG)
                return I_ERECORD;
            if (phys >= g_cfg.tbls[i].maxrec)
                return I_ENREC;
            
            
            fd = open_table_file(i, "DAT", O_RDWR);
            if (fd == ERROR)
                return I_EOPEN;
            
            nbytes = tsz;
            if (nbytes > I_RECSZ)
            {
                close(fd);
                return I_EUPDT;
            }
            offset = (long)phys * nbytes;
            if (engine_seek(fd, offset, 0) < 0L)
            {
                close(fd);
                return I_EUPDT;
            }
            if (engine_read(fd, oldrec, nbytes) != nbytes)
            {
                close(fd);
                return I_EREAD;
            }
            if (oldrec[0] == I_DELFLAG)
            {
                close(fd);
                return I_ENREC;
            }
            rc = prepare_key_changes(i, oldrec, rec, phys, key_changes);
            if (rc != I_OK)
            {
                close(fd);
                return rc;
            }
            if (engine_seek(fd, offset, 0) < 0L)
            {
                close(fd);
                return I_EUPDT;
            }
            new_metadata = metadata;
            new_metadata.generation++;
            if (engine_write(fd, rec, nbytes) != nbytes)
            {
                close(fd);
                return I_EUPDT;
            }
            if (fsync(fd) != 0)
            {
                close(fd);
                return I_EUPDT;
            }
            defer_table_file_invalidation = 1;
            rc = apply_key_changes(i, rec, phys, key_changes);
            defer_table_file_invalidation = 0;
            if (rc != I_OK)
            {
                close(fd);
                return rc;
            }
            return finish_mutation(i, fd, &new_metadata);
}

/* Delete record by physical slot */
int i_delphys(int tblnam, int phys)
{
    int fd;
    int i, tsz, nbytes;
    int rc;
    long offset;
    char *sbuf;
    struct persistent_metadata metadata;
    struct persistent_metadata new_metadata;
    
    sbuf = record_workspace;
    if (phys < 0)
        return I_ENREC;
    
    i = checked_table(tblnam);
    if (i < 0)
        return i;
            rc = load_table_metadata(i, &metadata);
            if (rc != I_OK)
                return rc;
            if (g_cfg.tbls[i].nrecs == 0)
                return I_ENREC;
            if (phys >= g_cfg.tbls[i].maxrec)
                return I_ENREC;
            
            tsz = g_cfg.tbls[i].recsz;
            
            fd = open_table_file(i, "DAT", O_RDWR);
            if (fd == ERROR)
                return I_EOPEN;
            
            nbytes = tsz;
            if (nbytes > I_RECSZ)
            {
                close(fd);
                return I_EUPDT;
            }
            
            offset = (long)phys * nbytes;
            if (engine_seek(fd, offset, 0) < 0L)
            {
                close(fd);
                return I_EUPDT;
            }
            
            if (engine_read(fd, sbuf, nbytes) != nbytes)
            {
                close(fd);
                return I_EREAD;
            }
            if (sbuf[0] == I_DELFLAG)
            {
                close(fd);
                return I_ENREC;
            }

            memcpy(key_record_workspace, sbuf, nbytes);
            
            new_metadata = metadata;
            new_metadata.active_count--;
            new_metadata.free_head = phys;
            new_metadata.generation++;
            sbuf[0] = I_DELFLAG;
            set_free_link(sbuf, metadata.free_head);
            if (engine_seek(fd, offset, 0) < 0L)
            {
                close(fd);
                return I_EUPDT;
            }
            if (engine_write(fd, sbuf, nbytes) != nbytes)
            {
                close(fd);
                return I_EUPDT;
            }
            if (fsync(fd) != 0)
            {
                close(fd);
                return I_EUPDT;
            }
            defer_table_file_invalidation = 1;
            rc = erase_keys(i, key_record_workspace, phys, 1);
            defer_table_file_invalidation = 0;
            if (rc != I_OK)
            {
                close(fd);
                return rc;
            }
            return finish_mutation(i, fd, &new_metadata);
}

/* Read record by index (0-based) from table data file */
#ifndef ISAM_BANK_LIBRARY
int i_rdrec(int tblnam, char *rec, int rnum)
{
    int phys;
    int rc;
    
    if (rnum < 0)
        return I_ENREC;
    
    /* Find physical index of logical record */
    rc = i_findlog(tblnam, rnum, &phys);
    if (rc != I_OK)
        return rc;
    return i_rdphys(tblnam, rec, phys);
}

/* Update record by index using temp file rewrite */
int i_uprec(int tblnam, char *rec, int rsiz, int rnum)
{
    int phys;
    int rc;
    
    if (rnum < 0)
        return I_ENREC;
    
    /* Find physical index of logical record */
    rc = i_findlog(tblnam, rnum, &phys);
    if (rc != I_OK)
        return rc;
    
    return i_wrphys(tblnam, rec, rsiz, phys);
}

/* Delete record by logical index via the indexed physical delete path. */
int i_delrec(int tblnam, int rnum)
{
    int phys;
    int rc;

    if (rnum < 0)
        return I_ENREC;

    rc = i_findlog(tblnam, rnum, &phys);
    if (rc != I_OK)
        return rc;

    return i_delphys(tblnam, phys);
}
#endif

#undef close
