import sys
import pysam
import subprocess

from Bio import SeqIO

reads_fasta = sys.argv[1]
reference_fasta = sys.argv[2]
grap_path = sys.argv[3]
K = sys.argv[4]
type = sys.argv[5]
idealreads_aligner = "/home/tdvorkina/tmp/algorithmic-biology/assembler/build/release/bin/idealreads_aligner"

def load_fasta(filename):
    record_dict = SeqIO.to_dict(SeqIO.parse(filename, "fasta"))
    return record_dict


def make_rc(seq):
    mapping = {"A": "T", "T": "A", "C": "G", "G": "C"}
    res = []
    for c in seq:
        res.append(mapping[c])
    return "".join(res[::-1])

def load_ideal_reads(reads_bam, reference_fasta, reads):
    readssam = pysam.AlignmentFile(reads_bam, "rb")
    reffasta = pysam.FastaFile(reference_fasta)

    mp = {}
    cnt = 0
    for r in readssam:
        start = r.reference_start
        end = r.reference_end
        name = r.query_name
        if start == -1 or end is None:
            continue
        sequence = reffasta.fetch(r.reference_name, start, end)
        cnt += 1
        if (name not in mp or len(mp[name]) < len(sequence)) and len(sequence) > 0.8*len(reads[name].seq):
            if r.is_reverse:
                sequence = make_rc(sequence)
            mp[name] = sequence
            mapped_ids.append(name)
    readssam.close()
    reffasta.close()
    print("Number of mapped items > 80%: " + str(len(mp)))
    print("Number of mapped items: " + str(cnt))
    return mp


def save(refseq_fasta, seq):
    with open(refseq_fasta, "w") as out:
        for k in seq.keys():
            out.write(">" + k + "\n" + seq[k] + "\n")


def run_bwamem(reads_fasta, reference_fasta, type, build_index = False):
    print("> Load reads from fasta")
    reads = load_fasta(reads_fasta)
    reads_bam = reads_fasta[:-len(".fasta")] + "_origin.bam"
    print("> Run bwa mem to align reads on reference and save result to " + reads_bam)
    #if build_index:
    print("> Build index")
    subprocess.call(["bwa index " + reference_fasta], shell=True)
    subprocess.call(["samtools faidx " + reference_fasta], shell=True)
    if type == "pacbio":
        t = "pacbio"
    elif type == "nanopore":
        t = "ont2d"
    print("bwa mem -x " + t +  " -t 33 " + reference_fasta + " " + reads_fasta + " | samtools view -bS - > " + reads_bam)
    subprocess.call(["bwa mem -x " + t + " -t 33 " + reference_fasta + " " + reads_fasta + " | samtools view -bS - > " + reads_bam], shell=True)
    print("> Load mappings from bam (longest mapping for each read name and not less 20% length)")
    refseq = load_ideal_reads(reads_bam, reference_fasta, reads)
    refseq_fasta =  "/".join(reads_fasta.split("/")[:-1] + [ "refseq_bwamem_" + reads_fasta.split("/")[-1]])
    print("> Save into " + refseq_fasta)
    save(refseq_fasta, refseq)
    return refseq_fasta

def run_minimap2(reads_fasta, reference_fasta, type, build_index = False):
    print("> Load reads from fasta")
    reads = load_fasta(reads_fasta)
    reads_bam = reads_fasta[:-len(".fasta")] + "_origin.bam"
    print("> Run minimap2 to align reads on reference and save result to " + reads_bam)
    #if build_index:
    #print("> Build index")
    #subprocess.call(["bwa index " + reference_fasta], shell=True)
    #subprocess.call(["samtools faidx " + reference_fasta], shell=True)
    if type == "pacbio":
       t = "map-pb"
    elif type == "nanopore":
       t = "map-ont"
    print("/home/tdvorkina/soft/minimap2/minimap2/minimap2 -ax " + t + " -t 33 " + reference_fasta + " " + reads_fasta + " | samtools view -bS - > " + reads_bam)
    subprocess.call(["/home/tdvorkina/soft/minimap2/minimap2/minimap2 -ax " + t + " -t 33 " + reference_fasta + " " + reads_fasta + " | samtools view -bS - > " + reads_bam], shell=True)
    print("> Load mappings from bam (longest mapping for each read name and not less 20% length)")
    refseq = load_ideal_reads(reads_bam, reference_fasta, reads)
    refseq_fasta =  "/".join(reads_fasta.split("/")[:-1] + [ "refseq_" + reads_fasta.split("/")[-1]])
    print("> Save into " + refseq_fasta)
    save(refseq_fasta, refseq)
    return refseq_fasta

def run_idealreads_aligner(refseq_fasta):
    print("> Run reference sequence aligner ")
    print(idealreads_aligner + " " + K + " " + grap_path + " " + refseq_fasta + " " + refseq_fasta + "_mapping > " + refseq_fasta + ".log")
    subprocess.call([idealreads_aligner + " " + K + " " + grap_path + " " + refseq_fasta + " " + refseq_fasta + "_mapping > " + refseq_fasta + ".log"], shell=True)


refseq_fasta = run_bwamem(reads_fasta, reference_fasta, type)
run_idealreads_aligner(refseq_fasta)

