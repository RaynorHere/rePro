(define (copy source target replacement)
  (cond ((equal? source target) replacement)
        ((not (pair? source)) source)
        (else (cons (copy (car source) target replacement) (copy (cdr source) target replacement)))
    )
  )

(define replace
  (lambda (source search-for replace-with)
    (copy source search-for replace-with)
    )
)

; (print(replace 1 1 2))
; (print(replace '(a (b c) d) '(b c) '(x y)))
; (print(replace '(a (b c) (d (b c))) '(b c) '(x y)))
; (print(replace '(a b c) '(a b) '(x y)))
; (print(replace '(a b c) '(b c) '(x y)))

; (newline)
; (display "More complex tests")
; (newline)
; (print(replace 1 2 3))
; (print(replace 1 1 2))
; (print(replace () 1 2))
; (print(replace () () 1))
; (print(replace () () '(1)))
; (print(replace '(1) () 2))
; (print(replace '(1) 1 2))
; (print(replace '((1) (1)) '(1) '(2)))
; (print(replace '(a (b c) d) '(b c) '(x y)))
; (print(replace '(a (b c) (d (b c))) '(b c) '(x y)))
; (print(replace '(a b c) '(a b) '(x y)))
; (print(replace '(a b c) '(b c) '(x y)))