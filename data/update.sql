\setrandom id 1 10000
BEGIN;
SELECT * FROM documents WHERE sentence %% random_word();
UPDATE documents SET sentence = random_sentence() WHERE id = :id;
END;
